# Offline audit

Glacier Ref must never originate network traffic (spec §1, acceptance A10).
The repeatable audit procedure (milestone M0.09) is used from M1 onward and
for the full A10 run in M4.

## Procedure (Linux)

```sh
sudo pacman -S strace            # once; Fedora: sudo dnf install strace
cmake --preset dev-release && cmake --build --preset dev-release
tools/offline-audit.sh build/dev-release/app/glacier-ref --platform-info
tools/offline-audit.sh build/dev-release/app/glacier-ref   # interactive: exercise the workflow, then quit
```

[`tools/offline-audit.sh`](../tools/offline-audit.sh):

1. starts the command in a new network namespace (`unshare --map-current-user
   --net`), so the process sees only a downed loopback interface and no
   connectivity;
2. traces `socket`, `connect`, `bind`, `sendto` and `sendmsg`, plus `execve`
   and process creation, for the whole process tree with `strace -f`;
3. attributes every socket call to a process. It **fails** if any
   `AF_INET`/`AF_INET6` socket activity comes from the application's own
   processes, even an attempt that could not succeed in the namespace;
4. lists the programs the application started. Sockets of the desktop's URL
   openers (`xdg-open`, `kde-open`, `kioclient`, `gio`, …) and everything they
   start (the browser after Help ▸ Project Website, a user-initiated link,
   spec A10) are counted separately and don't fail the audit; any other
   started program counts as the application's own;
5. lists `AF_UNIX` (Wayland, D-Bus, portals) and `AF_NETLINK` activity as
   expected local IPC.

`--keep-log` keeps the full trace for inspection.

### Scripted run (M4.08)

`glacier-bench` covers import (including SVG in the stress dataset), decoding,
rendering, an edit, save, a recovery snapshot and opening the saved board:

```sh
QT_QPA_PLATFORM=offscreen tools/offline-audit.sh build/dev-release/tools/bench/glacier-bench \
    --dataset /data/glacier-fixtures/stress --size 2560x1440 --frames 30 --store-root /data/glacier-fixtures
```

### Interactive run (A10)

Start `tools/offline-audit.sh build/dev-release/app/glacier-ref` on the
desktop and go through the list, then quit and record the result:

1. startup with the recovery dialog (leave a crashed session first: `kill -9` a running instance with unsaved changes)
2. Help ▸ Glacier Ref Help, Keyboard Shortcuts, About; Help ▸ Copy Diagnostics
3. Help ▸ Project Website (the browser opens; its traffic is reported as not counted)
4. import via dialog, folder, drag-and-drop from the file manager and the browser, clipboard paste; Import as Link
5. an SVG import; a drop of a web image (refused with the offline message)
6. editing: transforms, crop, groups, notes, undo/redo, Copy Image
7. Preferences: change a shortcut and the theme
8. save, Save As, open; a missing link located; recovery after `kill -9`

On macOS and Windows no automated tracer is set up; there the build-level
guarantees below apply, and the platform's own firewall log can be checked
during the manual validation (M5).

## Static guarantees

- The build links no Qt Network module (`docs/DEPENDENCIES.md`).
- The Flatpak (M5) has no network permission.

## Results

| Date | Commit | Command | Result |
| --- | --- | --- | --- |
| 2026-11-06 | `d9b86fd` | `glacier-ref --platform-info` | PASS: no AF_INET/AF_INET6 activity; 4 AF_UNIX calls |
| 2026-11-06 | `d9b86fd` | `timeout 5 glacier-ref` (main window on Wayland, no interaction) | PASS: no AF_INET/AF_INET6 activity; 6 AF_UNIX calls |
| 2026-11-06 | — | Negative control: Python `connect()` to 192.0.2.1:80 | FAIL as intended: `socket(AF_INET…)` and `connect` reported |
| 2026-11-06 | `28d2dd4` | `glacier-bench --dataset stress` (1,000 mixed images incl. 103 SVGs: import, decode, render, 2×60 frames; offscreen) | PASS: no AF_INET/AF_INET6 activity; 3 AF_UNIX calls |
| 2026-11-07 | `72f6e9c` | `glacier-bench --dataset standard` with the persistence phase (import, save, recovery snapshot, open) | PASS: no AF_INET/AF_INET6 activity; 4 AF_UNIX calls |
| 2026-11-07 | — | Negative controls for process attribution: a child exec'ing a fake `xdg-open` that opens an AF_INET socket; a child exec'ing another helper that does the same | As intended: the first PASSes with 1 call "not counted", the second FAILs |
| 2026-11-07 | M4 commit 6 | `glacier-bench --dataset stress` (import incl. 103 SVGs, decode, render, select/move, save, recovery snapshot, open) | PASS: no AF_INET/AF_INET6 activity; 6 AF_UNIX calls |

Environment: CachyOS, KDE Plasma 6.8.2 (Wayland), strace 7.3, util-linux 2.42.4.
