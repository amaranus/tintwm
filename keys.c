// This is where you create keybindings
// Just add an entry to the keys[] array in grab_keys
// and provide the action in the appropriate switch case in key_press

#include <X11/keysym.h>
#include <xcb/xcb_keysyms.h>
#include <X11/XF86keysym.h>

#include "keys.h"
#include "tintwm.h"

#define WIN XCB_MOD_MASK_4
#define ALT XCB_MOD_MASK_1
#define CONTROL XCB_MOD_MASK_CONTROL
#define SHIFT XCB_MOD_MASK_SHIFT

static xcb_key_symbols_t *keysyms;

void grab_keys(void)
{
  struct key
  {
    uint16_t mod;
    xcb_keysym_t key;
  };
  struct key keys[] = {      
/*    Ön tuş                      tuş        */
      { WIN | SHIFT,              XK_q },
      { WIN,                      XK_d },
      { WIN,                      XK_q },
      { WIN,                      XK_r },
      { WIN,                  XK_space },
      { ALT,                    XK_Tab },
      { 0,     XF86XK_AudioRaiseVolume },
      { 0,     XF86XK_AudioLowerVolume },
      { 0,            XF86XK_AudioMute },
  };

  keysyms = xcb_key_symbols_alloc(dpy);
  if (!keysyms)
    DIE("cannot allocate keysyms\n");

#define LENGTH(x) (sizeof(x) / sizeof(*x))
  for (size_t i = 0; i < LENGTH(keys); i++)
  {
    xcb_keycode_t *code = xcb_key_symbols_get_keycode(keysyms, keys[i].key);
    if (!code)
      continue;

    xcb_grab_key(dpy, 1, screen->root, keys[i].mod, *code, XCB_GRAB_MODE_ASYNC,
                 XCB_GRAB_MODE_ASYNC);

    free(code);
  }
}

void key_press(xcb_key_press_event_t *e)
{
  const xcb_keysym_t key = xcb_key_symbols_get_keysym(keysyms, e->detail, 0);

  switch (e->state)
  { // modifier
  case WIN | SHIFT:
    switch (key)
    {
    case XK_q:
      running = false;
      break;
    }
    break;
  case WIN:
    switch (key)
    {
    case XK_d:
      system("dmenu_run -l 10 -p 'Uygulama seç:' -fn 'Terminus-13' -nb "
             "'#353535' -sb '#0a0a0a'");
      break;
    case XK_space:
      layout = (layout + 1) % 3;
      arrange();
      break;
    case XK_q:
      if (focus)
        close_client(focus);
      break;    
    case XK_r:
        system("rofi -combi-modi window,drun,ssh -show combi -show-icons");
      break;
    }
    break;
  case ALT:
    switch (key)
    {
    case XK_Tab:
      if (focus && focus->focus_next)
        focus_client(focus->focus_next);
      else
        focus_client(master);
      break;
    }
    break;
  case 0: // modifier yok
    switch (key) {
    case XF86XK_AudioRaiseVolume:
      system("amixer -q set Master 5%+");
      break;
    case XF86XK_AudioLowerVolume:
      system("amixer -q set Master 5%-");
      break;
    case XF86XK_AudioMute:
      system("amixer -q set Master toggle");
      break;
    }
    break;
  }
}
