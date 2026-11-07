# M4 — Desktop validation checklist (maintainer)

Release build: `./build/dev-release/app/glacier-ref`. Record the environment first, then mark each row ✅ / ⚠️ / ❌ with a short note.
Rows marked **all** are repeated in each session type below; the others need only CachyOS Wayland.

| Session | OS / version | Desktop / compositor | Qt platform (`glacier-ref --platform-info`) | Scaling / displays |
| --- | --- | --- | --- | --- |
| A — CachyOS Wayland (reference) | | KDE Plasma … / KWin | `wayland` | 2560×1440 @ 100 %, 5120×1440 @ … |
| B — CachyOS X11 | | KDE Plasma … (X11 session) | `xcb` | |
| C — Fedora | edition, version | GNOME/KDE …, compositor | | |

## Minimal UI and help (M4.01)

| # | Step | Expected | A | B | C | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | First start with a fresh profile (`mv ~/.config/GlacierRef ~/.config/GlacierRef.bak` before, back after) | Dark Glacier look, no menu bar, ≡ button top-left, a hint about Tab / ≡ / right-click | | | | |
| 2 | **all** Press Tab; press Tab again; open a new window (Ctrl+N) | Menu bar shows/hides; new windows follow the last choice | | | | |
| 3 | Click ≡ with the menu bar hidden | The full menu (File … Help) opens | | | | |
| 4 | Right-click the canvas with the menu bar hidden | "Show Menu Bar" is the first entry | | | | |
| 5 | Edit a note and press Tab, N, G | The keys go into the note; nothing else happens | | | | |
| 6 | F1; Help ▸ Keyboard Shortcuts; type in the filter | Help opens; the table lists all commands; filter works | | | | |
| 7 | Help ▸ Project Website | The browser opens the GitHub page (only now) | | | | |
| 8 | Help ▸ Copy Diagnostics, paste into a text editor | Platform, folders and settings; paths start with `~`, no user name | | | | |

## Preferences (M4.02)

| # | Step | Expected | A | B | C | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 9 | Edit ▸ Preferences (Ctrl+,) ▸ Appearance ▸ Theme: System, OK; then back to Glacier dark | The whole UI switches live both ways | | | | |
| 10 | Shortcuts: give Crop the shortcut Ctrl+S | Conflict shown, OK disabled; set it back with "Default" | | | | |
| 11 | Shortcuts: give Grayscale Ctrl+Shift+Y, OK; try it in two open windows; restart the app | Works everywhere and after restart; "Restore All Defaults" undoes it | | | | |
| 12 | General: image cache 1024 MiB, recovery 30 s, OK; Help ▸ Copy Diagnostics | The new values are listed | | | | |
| 13 | Appearance: background for new boards, OK, Ctrl+N | The new board uses it (not marked as changed) | | | | |

## Window integration (M4.03, M4.04)

| # | Step | Expected | A | B | C | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 14 | **all** Resize/maximize a window, close it, start the app again | The size (and maximized state) comes back | | | | |
| 15 | **all** View ▸ Always on Top, then click another app's window | Wayland: "requested — your compositor decides" + guidance; X11/others: stays on top | | | | |
| 16 | **all** View ▸ Window Opacity | Wayland: not in the menu; X11: 80 % makes the window translucent | | | | |
| 17 | **all** View ▸ Frameless Window; drag the top band; double-click it; Alt+drag the canvas | The title bar disappears; the window moves; maximize toggles; Alt+drag moves | | | | |
| 18 | **all** In frameless mode: drag each edge and a corner | The window resizes | | | | |
| 19 | **all** In frameless mode: Escape, then again via right-click ▸ Show Window Frame, then via ≡ ▸ View | The frame comes back each time; Ctrl+W still closes | | | | |
| 20 | Restart while frameless | Starts frameless; the way back still works | | | | |

## Desktop, DPI and input (M4.05, M4.06)

| # | Step | Expected | A | B | C | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 21 | **all** Drag a window between the two displays (mixed DPI) and keep working | Images, handles and text stay sharp; handles stay the same on-screen size | | | | |
| 22 | Set display scaling to 125 %, then 150 % (log out/in if needed); use handles, crop and notes | Hit targets match what you see; nothing blurry or cut off | | | | |
| 23 | **all** Drag images from the file manager and a browser; paste from the clipboard; Copy Image into another app | All work as in M1/M2 | | | | |
| 24 | **all** Touchpad: two-finger scroll pans, pinch zooms | Smooth, anchored at the fingers | | | | |
| 25 | **all** Open and save dialogs, Preferences and Help with the keyboard (Tab, Enter, Escape) | Focus moves in the dialog; after closing, keys go to the canvas again | | | | |
| 26 | Save/open a board in a folder named `Réf ä ✓` with a file `Mood ✓ 1.glacierref` | Works; the title shows the name correctly | | | | |

## Milestone demo (M4)

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 27 | Use Glacier Ref beside a creative app (Krita, Blender, GIMP) on CachyOS Wayland for a while: menus hidden, frameless, on the second display | Stays out of the way; every command reachable | | |
| 28 | Change shortcuts to your taste, move between displays, open the standard dataset board and navigate | Smooth navigation | | |
