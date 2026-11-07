# M3 — Durable boards demo checklist (maintainer)

Release build: `./build/dev-release/app/glacier-ref` on CachyOS Wayland, plus the Windows machine for steps 12–14.
Recovery data lives in `~/.local/share/GlacierRef/GlacierRef/recovery` (Windows: `%APPDATA%\GlacierRef\GlacierRef\recovery`).
Mark each row ✅ / ⚠️ / ❌ with a short note.

Result (2026-11-07, maintainer; CachyOS KDE Plasma Wayland and the Windows machine; build `6ed4d17`): **23/24 pass, 1 issue** (step 23), fixed afterwards; re-check of step 23 pending.

Preparation: a folder `~/demo/refs` with 3–4 images outside the board folder, and a board folder `~/demo/boards`.

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 1 | New board: drop 3 images, File ▸ Import as Link (Ctrl+Shift+I) two images from `~/demo/refs`, add a note with umlauts/emoji, crop + rotate one image, group two items, change background, G | Everything appears as in M2 |✅ | |
| 2 | Ctrl+S, save as `~/demo/boards/demo.glacierref` | Title shows `demo.glacierref` without `*`; `ls -a ~/demo/boards` shows `.demo.glacierref.lock` | ✅| |
| 3 | Pan/zoom somewhere, Ctrl+W, then File ▸ Open (Ctrl+O) the board | Opens in the same (empty) window with everything as saved, including crop, rotation, group, note, background, grayscale and the saved view |✅ | |
| 4 | With the board open, run `glacier-ref ~/demo/boards/demo.glacierref` in a terminal | Second process: "Opened Read-Only" notice; title says "(read-only)"; Save disabled; edits possible; Save As to another name works | ✅| |
| 5 | Double-click the note, type text, press Ctrl+S without leaving the note | The note is committed and saved; the title has no `*` |✅ | |
| 6 | Make an edit, then `touch ~/demo/boards/demo.glacierref`, then Ctrl+S | "Changed by another program" with Save As… / Replace / Cancel; Cancel leaves the file alone, Replace saves |✅ | |
| 7 | Save As into `~/demo/boards/archive/demo2.glacierref` (new folder), close, open it | The linked images are still found (paths were rebased) |✅  | |
| 8 | Close it, move one linked image to `~/demo/moved/`, open the board again | Summary "1 linked image could not be found"; the item is a labelled placeholder of the same size |✅  | |
| 9 | Select the placeholder, Board ▸ Links ▸ Locate Linked File…, pick the moved file | The image appears; Ctrl+Z shows the placeholder again, Ctrl+Shift+Z the image |✅  | |
| 10 | Edit the other linked image in GIMP/Krita (e.g. paint on it, save), select it, Board ▸ Links ▸ Reload | It turns into a "changed on disk" placeholder (never replaced silently); Accept Changed File shows the new version | ✅| |
| 11 | Board ▸ Links ▸ Embed All Linked Images, Ctrl+S | The board no longer depends on `~/demo/refs` | ✅| |
| 12 | Copy `demo.glacierref` (only the board file) to the Windows machine and open it there | Everything matches, without the source images (A03) |✅ | |
| 13 | On Windows: move an item, save, copy the board back to Linux and open it | The change is there |✅ | |
| 14 | On Windows: open the board twice (two app instances) | The second instance is read-only, as on Linux | ✅| |
| 15 | Back on Linux: open `demo.glacierref`, make several edits, wait over a minute, then `kill -9 $(pgrep -f build/dev-release/app/glacier-ref)` | (The app disappears) |✅| |
| 16 | In another window before the kill (or separately): a new, never-saved board with images and a note, also older than a minute when killed | (Covered by the same kill) | ✅| |
| 17 | Start the app again | Recovery dialog lists both boards (the saved one with its path, the other as "(never saved)") | ✅| |
| 18 | Recover both | Two windows, both marked changed (`*`); the named one shows `demo.glacierref` and the latest edits; the unsaved one is "Untitled" (A06) | ✅| |
| 19 | Save the named board (Ctrl+S) and close; close the unnamed one and choose Discard | No prompts afterwards; on the next start no recovery dialog appears | ✅| |
| 20 | Repeat 15 + 17 with one board, choose Open Original, quit, start again, choose Discard… | Open Original shows the saved version and the recovery is offered again; Discard removes it for good | ✅| |
| 21 | Close a window with unsaved changes; try Cancel, then Save, then Discard on separate attempts | Cancel keeps the window; Save saves (asking for a name if untitled) and closes; Discard closes | ✅| |
| 22 | Drag `demo.glacierref` from Dolphin onto an empty window | The board opens | ✅| |
| 23 | Rename a JPEG to `broken.glacierref` and open it | Clear error ("could not be opened"), the app keeps running |✅ | the app keeps running but clicking on "ok" does not not close the dialog, first the winddow with the progress bar needs to be closed via clicking on "x" → Cause: the opening progress dialog was only scheduled for deletion and appeared (window-modal) over the error box. Fixed: progress dialogs are deleted before any message box. Re-check pending |
| 24 | Open a board from a folder you can't write to (e.g. copy it to a read-only USB stick or `chmod a-w` a folder) | Opens read-only with "the folder is not writable" | ✅| |
