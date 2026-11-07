# M2 — Editing demo checklist (maintainer)

Release build on CachyOS Wayland: `./build/dev-release/app/glacier-ref`.
Mark each row ✅ / ⚠️ / ❌ with a short note.

Result (2026-11-07, maintainer, CachyOS KDE Plasma Wayland, build `7cc43d5`): **19/19 pass.** Two notes are feature requests for later versions (rows 12 and 17), not failures.

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 1 | Import 6–8 images (drop or Ctrl+I) | Placed and selected; one Undo removes the batch | ✅|  |
| 2 | Drag an image; drag a corner handle; Shift+edge handle | Moves; resizes keeping aspect; Shift stretches one axis | ✅| |
| 3 | Rotate with the round handle; then with Shift held | Free rotation; Shift snaps to 15° |✅ | |
| 4 | Start a drag, press Escape before releasing | Item jumps back; nothing in Undo | ✅| |
| 5 | H / V on a multi-selection | Arrangement mirrors |✅ | |
| 6 | C on a rotated image, drag crop edges, Enter; then C + Escape | Crop applied in place; Escape cancels |✅ | |
| 7 | Select two items, Ctrl+G; double-click a member; move it; Escape | Group created; member edited inside; Escape leaves group | ✅| |
| 8 | Ctrl+Shift+G on the group | Members stay exactly where they were |✅ | |
| 9 | Ctrl+L on an item; try to drag, delete, crop it | Nothing changes; "Locked" badge; toast for mixed selections |✅ | |
| 10 | Board → Align ▸ and Arrange (with a locked item present) | Aligned; arranged in rows; locked item untouched | ✅ | |
| 11 | Ctrl+PgUp / Ctrl+Shift+PgDn | Stacking order changes | ✅| |
| 12 | N, type a note (try F, H, Delete, Ctrl+Z inside), Escape | Keys go to the text; one Undo step for the whole note |✅ | Works, but notes should be customizeable in style, can be implemented later on, after v1 |
| 13 | Resize the note by its side handles; Note Style… | Text reflows; style changes |✅ | |
| 14 | G (grayscale), Board Background… | Images gray (notes not); background changes; both undoable | ✅| |
| 15 | Ctrl+N; in window 1 select some items, Ctrl+C; in window 2 Ctrl+V | Items appear in window 2 with images |✅ | |
| 16 | Ctrl+D; Ctrl+X then Ctrl+V | Duplicate offset; cut/paste works | ✅| |
| 17 | Select one image, Ctrl+Shift+C, paste into another app (e.g. Krita, GIMP) | Pasted with crop/flip applied |✅| crop and flip was applied, if possible rotation would also be useful |
| 18 | Right-click on an item and on empty canvas | Context menu shows relevant actions | ✅| |
| 19 | Undo everything (Ctrl+Z repeatedly), then Redo everything | Board returns to empty, then to the final state exactly | ✅| |
