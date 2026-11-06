# M1.12 — Native Wayland feasibility checklist

Run on the reference workstation in a normal Plasma session with a Release build
(`cmake --build --preset dev-release`). Record the result (✅ / ⚠️ / ❌) and a short note.
Findings that need work are assigned to M4.

**Result (2026-11-06, maintainer):** all checks passed; always-on-top is ignored by KWin on Wayland (expected, see #17).

Environment (prefilled): CachyOS, Linux 7.3.1-1-cachyos, KDE Plasma 6.8.2, KWin (Wayland),
Qt 6.12.0 (`wayland` platform plugin), screens HDMI-A-1 2560×1440 @ 144 Hz and DP-1 5120×1440 @ 120 Hz, DPR 1.

Start: `./build/dev-release/app/glacier-ref` (check `--platform-info` reports `wayland`).

| # | Check | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 1 | Drop 3–5 images from Dolphin | Imported around the drop point, selected | ✅ |  |
| 2 | Drop a folder from Dolphin (with a subfolder and a .txt) | Top-level images imported; report names skipped subfolder/file | ✅ |  |
| 3 | Drag an image from Firefox onto the board | Imported from image data, or the offline message if Firefox only offers the link | ✅ |  |
| 4 | Drag an image from Chromium onto the board | As 3 | ✅ |  |
| 5 | Right-click image in Firefox → Copy Image, then Ctrl+V | Imported as PNG | ✅ |  |
| 6 | Copy a link (text) to an image, Ctrl+V | Offline message, nothing imported | ✅ |  |
| 7 | Copy a file in Dolphin (Ctrl+C), Ctrl+V | File imported (original bytes) | ✅ |  |
| 8 | File → Import Images… and Import Folder… | Dialog opens (portal or Qt), import works, last folder remembered | ✅ |  |
| 9 | Pan (middle drag, Space+drag), wheel zoom at cursor, F / Shift+F | Smooth; point under cursor stays fixed | ✅ |  |
| 10 | Touchpad (if available): two-finger scroll pans, pinch zooms |  | ✅ |  |
| 11 | Click, Shift+click, rubber band, Shift+rubber band | Outline visible on light and dark images | ✅ |  |
| 12 | Import the stress set (`/data/glacier-fixtures/stress`) and press Cancel during import | Dialog closes, already imported images appear, app stays responsive | ✅ |  |
| 13 | Close the window during a large import | No crash, process exits | ✅ |  |
| 14 | Move and resize the window; maximize; move between the two screens | Normal KWin behavior | ✅ |  |
| 15 | Set display scale to 125% (System Settings → Display) and repeat 9 and 11 | Sharp rendering, correct outlines | ✅ |  |
| 16 | Focus: switch to another app and back; Space-pan still works and no stuck hand cursor |  | ✅ |  |
| 17 | View → Always on Top: does the window stay above other windows? | Hint likely ignored on Wayland; message explains Alt+F3 → Keep Above Others / window rule | ⚠️ expected | KWin ignores the hint, as anticipated; the app shows its guidance message. Honest capability status → M4.03 |
| 18 | `glacier-bench --dataset /data/glacier-fixtures/standard --screen HDMI-A-1 --json /data/glacier-fixtures/results/m1-wayland-standard.json` | Runs full screen on the 2560×1440 output; record the summary line | ✅ | Ran full screen; JSON not kept, so the offscreen constrained run in M1.md remains the recorded baseline |
| 19 | `tools/offline-audit.sh build/dev-release/app/glacier-ref`, import via drop and paste, quit | PASS | ✅ |  |
