# tintwm - tiny tiling window manager

Inspired by [tinywm](http://incise.org/tinywm.html) and [catwm](https://github.com/pyknite/catwm). It's essentially a tiling version of tinywm, or alternatively, a stripped down version of catwm. I wanted to see what absolute minimum would be required for a fast, simple, minimally functional tiling window manager. catwm was the closest I've found, but even that had too many features, and it also had some bugs.

tintwm just tiles your windows. It has only one tiling layout (the verticle layout) and only one keybinding (mod-shift-q to quit). That's it. It does nothing else. It has no status bar, no program launchers, no fancy tiling layouts, no workspaces, no configuration file, no borders, no moving windows around, and no resizing windows.

Like tinywm, it's not designed to be configured or even used as-is. It's designed to be hacked. The code is so small and simple that you can read the code quickly. I believe most features people like can be placed in separate header files and included. Some examples are provided in the contrib directory.

The source code is split into three parts. The main functionality is in tintwm.c. The keybindings and associated actions can be placed in keys.c. The tiling layouts can be placed in layouts.c.

### Upgraded version notes
Upgraded from [tintwm](https://github.com/marshallmason/tintwm), this version adds horizontal mode and fullscreen float mode in addition to the existing vertical mode. The layout mode can be toggled using the ```super + space``` key combination. You can switch between two active windows with ```alt + tab```. These two windows are selected when in either float or tiled mode. The dmenu launcher can be started with ```super + s```. Finally, the selected window can be closed with ```super + s```. This makes it a lightweight window manager that's now a bit more functional.

Contact: amaranus@hotmail.com


