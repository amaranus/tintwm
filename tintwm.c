#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_ewmh.h>

#include "keys.h"
#include "layouts.h"
#include "tintwm.h"

xcb_connection_t *dpy;
xcb_screen_t *screen;
xcb_cursor_context_t *ctx;
struct client *focus, *master;
bool running = true;
uint16_t sw, sh;
xcb_ewmh_connection_t *ewmh;

void update_client_list(void) {
  // Pencere sayısını say
  int count = 0;
  for (struct client *c = master; c; c = c->next)
    count++;

  // Pencere ID'lerini bir diziye kopyala
  xcb_window_t *windows = malloc(sizeof(xcb_window_t) * count);
  int i = 0;
  for (struct client *c = master; c; c = c->next)
    windows[i++] = c->window;

  // _NET_CLIENT_LIST'i güncelle
  xcb_ewmh_set_client_list(ewmh, 0, count, windows);

  free(windows);
}

void move_resize(struct client *c, int16_t x, int16_t y, uint16_t w,
                 uint16_t h) {
  if (!c)
    return;

  uint16_t mask = 0;
  uint32_t values[4] = {0};
  int i = 0;

#define SET_GEOMETRY(field, xcb_field, force_set)                              \
  if (force_set || field != c->field) {                                        \
    c->field = field;                                                          \
    mask |= XCB_CONFIG_WINDOW_##xcb_field;                                     \
    values[i++] = field;                                                       \
  }

  const bool force = (c->w == 0);
  SET_GEOMETRY(x, X, force);
  SET_GEOMETRY(y, Y, force);
  SET_GEOMETRY(w, WIDTH, force);
  SET_GEOMETRY(h, HEIGHT, force);

  if (!mask)
    return;
  xcb_configure_window(dpy, c->window, mask, values);
}

static void add_focus(struct client *c) {
  if (c == focus)
    return;

  // Detach client from focus stack
  struct client *prev = focus;
  for (; prev && prev->focus_next != c; prev = prev->focus_next)
    ;
  if (prev)
    prev->focus_next = c->focus_next;

  // Push client to top of focus stack
  c->focus_next = focus;
  focus = c;
}

static void raise_window(xcb_window_t window) {
  const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
  xcb_configure_window(dpy, window, XCB_CONFIG_WINDOW_STACK_MODE, values);
  xcb_set_input_focus(dpy, XCB_INPUT_FOCUS_PARENT, window, XCB_CURRENT_TIME);
}

void focus_client(struct client *c) {
  if (!c)
    return;
  add_focus(c);
  raise_window(c->window);

  // Aktif pencereyi EWMH üzerinden bildir
  xcb_ewmh_set_active_window(ewmh, 0, c->window);
}

void close_client(struct client *c) {
  if (!c)
    return;

  xcb_intern_atom_cookie_t protocols_cookie =
      xcb_intern_atom(dpy, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
  xcb_intern_atom_reply_t *protocols_reply =
      xcb_intern_atom_reply(dpy, protocols_cookie, NULL);
  if (!protocols_reply)
    return;

  xcb_intern_atom_cookie_t delete_cookie =
      xcb_intern_atom(dpy, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
  xcb_intern_atom_reply_t *delete_reply =
      xcb_intern_atom_reply(dpy, delete_cookie, NULL);
  if (!delete_reply) {
    free(protocols_reply);
    return;
  }

  xcb_atom_t wm_protocols = protocols_reply->atom;
  xcb_atom_t wm_delete = delete_reply->atom;

  free(protocols_reply);
  free(delete_reply);

  // Check if the window supports WM_DELETE_WINDOW
  xcb_get_property_cookie_t prop_cookie =
      xcb_get_property(dpy, 0, c->window, wm_protocols, XCB_ATOM_ATOM, 0, 1024);
  xcb_get_property_reply_t *prop_reply =
      xcb_get_property_reply(dpy, prop_cookie, NULL);
  if (!prop_reply)
    return;

  bool supports_delete = false;
  if (prop_reply->type == XCB_ATOM_ATOM) {
    xcb_atom_t *atoms = (xcb_atom_t *)xcb_get_property_value(prop_reply);
    int len = xcb_get_property_value_length(prop_reply) / sizeof(xcb_atom_t);
    for (int i = 0; i < len; i++) {
      if (atoms[i] == wm_delete) {
        supports_delete = true;
        break;
      }
    }
  }
  free(prop_reply);

  if (supports_delete) {
    xcb_client_message_event_t ev = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .sequence = 0,
        .window = c->window,
        .type = wm_protocols,
        .data.data32 = {wm_delete, XCB_CURRENT_TIME, 0, 0, 0}};
    xcb_send_event(dpy, 0, c->window, XCB_EVENT_MASK_NO_EVENT,
                   (const char *)&ev);
    xcb_flush(dpy);
  } else {
    // fallback: forcibly kill if WM_DELETE is not supported
    xcb_kill_client(dpy, c->window);
    xcb_flush(dpy);
  }
}

static void activate_wm(void) {
  const xcb_event_mask_t event_mask[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                                         XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY};

  const xcb_void_cookie_t attr = xcb_change_window_attributes_checked(
      dpy, screen->root, XCB_CW_EVENT_MASK, event_mask);

  xcb_generic_error_t *err = xcb_request_check(dpy, attr);
  if (err)
    DIE("another window manager is running\n");
}

static void grab_buttons(void) {
  xcb_grab_button(dpy, 1, screen->root,
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
                  XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, screen->root,
                  XCB_NONE, XCB_BUTTON_INDEX_ANY, XCB_MOD_MASK_ANY);
}

static void setup(void) {

  int screen_num;
  dpy = xcb_connect(NULL, &screen_num);
  if (xcb_connection_has_error(dpy))
    exit(1);

  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(dpy));

  for (int i = 0; i < screen_num; i++)
    xcb_screen_next(&iter);

  screen = iter.data;

  master = focus = NULL;
  sw = screen->width_in_pixels;
  sh = screen->height_in_pixels;

  activate_wm();
  grab_keys();
  grab_buttons();

  grab_buttons();

  // Cursor ayarla
  if (xcb_cursor_context_new(dpy, screen, &ctx) >= 0) {
    xcb_cursor_t cursor = xcb_cursor_load_cursor(ctx, "default");
    if (cursor != XCB_CURSOR_NONE) {
      uint32_t values[] = {cursor};
      xcb_change_window_attributes(dpy, screen->root, XCB_CW_CURSOR, values);
      xcb_free_cursor(dpy, cursor);
    }
    xcb_cursor_context_free(ctx);
  }

  // EWMH başlatma
  ewmh = calloc(1, sizeof(xcb_ewmh_connection_t));
  xcb_intern_atom_cookie_t *cookie = xcb_ewmh_init_atoms(dpy, ewmh);
  if (!xcb_ewmh_init_atoms_replies(ewmh, cookie, NULL)) {
    DIE("EWMH atomlarını başlatamadı\n");
  }

  // Pencere yöneticisi olduğumuzu belirt
  xcb_ewmh_set_wm_name(ewmh, screen->root, strlen("tintwm"), "tintwm");
  xcb_ewmh_set_supporting_wm_check(ewmh, screen->root, screen->root);

  // Desteklenen özellikleri belirt
  xcb_atom_t supported[] = {
      ewmh->_NET_SUPPORTED,           ewmh->_NET_WM_NAME,
      ewmh->_NET_WM_WINDOW_TYPE,      ewmh->_NET_ACTIVE_WINDOW,
      ewmh->_NET_SUPPORTING_WM_CHECK, ewmh->_NET_CLIENT_LIST};
  xcb_ewmh_set_supported(ewmh, 0, sizeof(supported) / sizeof(xcb_atom_t),
                         supported);
}

static struct client *find_client(xcb_window_t window) {
  for (struct client *c = master; c; c = c->next)
    if (c->window == window)
      return c;
  return NULL;
}

static struct client *add_window(xcb_window_t window) {
  struct client *c = find_client(window);
  if (c)
    return c;

  c = calloc(1, sizeof(struct client));
  if (!c)
    DIE("unable to allocate memory\n");

  c->window = window;

  struct client *last = master;
  for (; last && last->next; last = last->next)
    ;
  if (last)
    last->next = c;
  else
    master = c;

  return c;
}

static void map_request(xcb_map_request_event_t *e) {
  xcb_intern_atom_cookie_t net_wm_window_type_cookie = xcb_intern_atom(
      dpy, 0, strlen("_NET_WM_WINDOW_TYPE"), "_NET_WM_WINDOW_TYPE");
  xcb_intern_atom_reply_t *reply_window_type =
      xcb_intern_atom_reply(dpy, net_wm_window_type_cookie, NULL);

  xcb_intern_atom_cookie_t net_wm_window_type_dialog_cookie =
      xcb_intern_atom(dpy, 0, strlen("_NET_WM_WINDOW_TYPE_DIALOG"),
                      "_NET_WM_WINDOW_TYPE_DIALOG");
  xcb_intern_atom_reply_t *reply_window_type_dialog =
      xcb_intern_atom_reply(dpy, net_wm_window_type_dialog_cookie, NULL);

  xcb_intern_atom_cookie_t wm_transient_for_cookie =
      xcb_intern_atom(dpy, 0, strlen("WM_TRANSIENT_FOR"), "WM_TRANSIENT_FOR");
  xcb_intern_atom_reply_t *reply_transient_for =
      xcb_intern_atom_reply(dpy, wm_transient_for_cookie, NULL);

  xcb_atom_t net_wm_window_type = reply_window_type->atom;
  xcb_atom_t net_wm_window_type_dialog = reply_window_type_dialog->atom;
  xcb_atom_t wm_transient_for = reply_transient_for->atom;
  free(reply_window_type);
  free(reply_window_type_dialog);
  free(reply_transient_for);

  // TRANSIENT pencere kontrolü
  xcb_get_property_cookie_t trans_ck = xcb_get_property(
      dpy, 0, e->window, wm_transient_for, XCB_ATOM_WINDOW, 0, 1);
  xcb_get_property_reply_t *trans_reply =
      xcb_get_property_reply(dpy, trans_ck, NULL);

  if (trans_reply && xcb_get_property_value_length(trans_reply)) {
    free(trans_reply);
    xcb_map_window(dpy, e->window);
    xcb_configure_window(dpy, e->window, XCB_CONFIG_WINDOW_STACK_MODE,
                         (uint32_t[]){XCB_STACK_MODE_ABOVE});
    xcb_flush(dpy);
    return;
  }
  free(trans_reply);

  // DIALOG pencere kontrolü
  xcb_get_property_cookie_t type_ck = xcb_get_property(
      dpy, 0, e->window, net_wm_window_type, XCB_ATOM_ATOM, 0, 8);
  xcb_get_property_reply_t *type_reply =
      xcb_get_property_reply(dpy, type_ck, NULL);

  if (type_reply) {
    xcb_atom_t *atoms = xcb_get_property_value(type_reply);
    int len = xcb_get_property_value_length(type_reply) / sizeof(xcb_atom_t);
    for (int i = 0; i < len; i++) {
      if (atoms[i] == net_wm_window_type_dialog) {
        free(type_reply);
        xcb_map_window(dpy, e->window);
        xcb_configure_window(dpy, e->window, XCB_CONFIG_WINDOW_STACK_MODE,
                             (uint32_t[]){XCB_STACK_MODE_ABOVE});
        xcb_flush(dpy);
        return;
      }
    }
  }
  free(type_reply);

  // Artık normal pencere: client listesine ekle
  struct client *c = add_window(e->window);
  add_focus(c);
  arrange();
  xcb_map_window(dpy, e->window);
  raise_window(e->window);
  update_client_list();
}

static void button_press(xcb_button_press_event_t *e) {
  struct client *c = find_client(e->child);
  if (c)
    focus_client(c);
  xcb_allow_events(dpy, XCB_ALLOW_REPLAY_POINTER, e->time);
}

static void configure_request(xcb_configure_request_event_t *e) {
  if (find_client(e->window))
    return;

  uint32_t values[6] = {0};
  int i = 0;

#define SET_VALUE(lowercase, uppercase)                                        \
  if (e->value_mask & XCB_CONFIG_WINDOW_##uppercase)                           \
    values[i++] = e->lowercase;

  SET_VALUE(x, X);
  SET_VALUE(y, Y);
  SET_VALUE(width, WIDTH);
  SET_VALUE(height, HEIGHT);
  SET_VALUE(sibling, SIBLING);
  SET_VALUE(stack_mode, STACK_MODE);

  xcb_configure_window(dpy, e->window, e->value_mask, values);
}

static void destroy_notify(xcb_destroy_notify_event_t *e) {
  struct client *c = find_client(e->window);
  if (!c)
    return;

  // Detach from clients list
  struct client *prev = master;
  if (!master->next) { // only one client
    master = NULL;
  } else if (master == c) { // first client
    master = c->next;
  } else { // somewhere in the middle, or end
    for (; prev && prev->next != c; prev = prev->next)
      ;
    prev->next = c->next;
  }

  // Detach from focus stack
  if (!focus->focus_next) { // only one client
    focus = NULL;
  } else if (focus == c) { // first client
    focus = c->focus_next;
  } else { // somewhere in the middle, or end
    prev = focus;
    for (; prev && prev->focus_next != c; prev = prev->focus_next)
      ;
    prev->focus_next = c->focus_next;
  }

  free(c);

  arrange();
  focus_client(focus);
  update_client_list();
}

static void run(void) {
  while (running) {
    xcb_flush(dpy);
    xcb_generic_event_t *ev = xcb_wait_for_event(dpy);
    switch (ev->response_type & ~0x80) {
    case XCB_KEY_PRESS:
      key_press((xcb_key_press_event_t *)ev);
      break;
    case XCB_BUTTON_PRESS:
      button_press((xcb_button_press_event_t *)ev);
      break;
    case XCB_CONFIGURE_REQUEST:
      configure_request((xcb_configure_request_event_t *)ev);
      break;
    case XCB_MAP_REQUEST:
      map_request((xcb_map_request_event_t *)ev);
      break;
    case XCB_DESTROY_NOTIFY:
      destroy_notify((xcb_destroy_notify_event_t *)ev);
      break;
    case XCB_CLIENT_MESSAGE: {
      xcb_client_message_event_t *e = (xcb_client_message_event_t *)ev;
      if (e->type == ewmh->_NET_ACTIVE_WINDOW) {
        struct client *c = find_client(e->window);
        if (c) {
          focus_client(c);
          arrange();
        }
      }
      break;
    }
    }
    free(ev);
    if (xcb_connection_has_error(dpy))
      DIE("the server closed the connection\n");
  }
}

int main(void) {
  setup();
  run();
  xcb_ewmh_connection_wipe(ewmh);
  free(ewmh);
  xcb_disconnect(dpy);
  return EXIT_SUCCESS;
}
