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
2. traces `socket`, `connect`, `bind`, `sendto`, and `sendmsg` for the whole
   process tree with `strace -f`;
3. **fails** if any `AF_INET`/`AF_INET6` socket activity appears, even an
   attempt that could not succeed in the namespace;
4. lists `AF_UNIX` (Wayland, D-Bus, portals) and `AF_NETLINK` activity as
   expected local IPC.

`--keep-log` keeps the full trace for inspection.

For interactive runs, record which workflows were exercised (startup, import
via dialog/drop/paste, SVG import, editing, save, recovery, help). The Help
menu's repository link opens the user's browser; that traffic belongs to the
browser, not Glacier Ref, and must be excluded from the run (or the link
not clicked).

## Static guarantees

- The build links no Qt Network module (`docs/DEPENDENCIES.md`).
- The Flatpak (M5) has no network permission.

## Results

| Date | Commit | Command | Result |
| --- | --- | --- | --- |
| — | — | `--platform-info` | Pending: `strace` not yet installed on the reference machine |
