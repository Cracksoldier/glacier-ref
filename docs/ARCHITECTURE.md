# Architecture and contracts

Milestone M0.08. This document fixes the rules later milestones build on. The
specification (§6) remains authoritative; this file makes its architecture
concrete and records decisions made during implementation.

## Components and build targets

| Directory | Target | Depends on | Responsibility |
| --- | --- | --- | --- |
| `core/` | `glacier::core` | Qt Core | `BoardDocument`, `Affine2D`, `Limits`, `BatchLayout`, `SessionAssetStore`, `link_paths`, IDs, app identity. **No Gui/Widgets.** |
| `io/` | `glacier::io` | core, Qt Gui, Svg, Concurrent | Format allowlist, `probeImage`/`decodeImage` (orientation, sRGB), `validateSvg`, `MimeRouter`, `ImportService` |
| `persistence/` | `glacier::persistence` | core, io, miniz | `ZipArchive`, `Manifest` (schema 1, migrations), `BoardSerializer` (save/load jobs, link checks), `DocumentLock`, `RecoveryStore` ([format](FORMAT.md)) |
| `editing/` | `glacier::editing` | core, io, Qt Gui | `CommandController` (QUndoStack), `Operations` (pure edit builders), `ClipboardFragment` (CBOR), `NoteLayout`, `Geometry` |
| `canvas/` | `glacier::canvas` | editing, io, Qt Widgets | `ImageCache`, `BoardScene`/`BoardView`, `BoardItems` (`ImageItem`, `NoteItem`, `GroupItem`), `TransformTool`: presentation and interaction only |
| `platform/` | `glacier::platform` | Qt Gui | `PlatformInfo` (M0); `PlatformWindowService` capabilities (M4) |
| `app/` | `glacier::app`, `glacier-ref` | all | `AppServices` (process-wide store, pool, cache, recovery), `MainWindow` (actions, import, board files, links, recovery), `RecoveryDialog`; `SettingsService` later |
| `tools/` | `glacier-fixturegen`, `glacier-bench` | app libs | Dataset generator and benchmark (not shipped) |
| `third_party/miniz` | `glacier::miniz` | — | Vendored ZIP/deflate (see [DEPENDENCIES.md](DEPENDENCIES.md)) |
| `tests/` | `tst_*` | as needed | Qt Test suites, each run in default and `de_AT` locale |

Dependency direction: `app → canvas → editing → io → core`, `app → persistence → io → core` (and `app → platform → core`). `core` never
includes Gui or Widgets headers; `canvas` items never own persistent state.

## M1 implementation notes

- **Import flow:** `MimeRouter` turns drops, the clipboard and dialogs into an `ImportPlan`. `ImportService` runs one job per source on the shared pool (`QtConcurrent::mapped`): read ≤ 100 MiB → probe → 256 px thumbnail → `SessionAssetStore::put`. Results arrive through a `QFutureWatcher` owned by the window's service and are applied by `MainWindow` as one `addImages` call, placed by `layoutBatch` around the drop point or view centre. There are no placeholders: a non-modal progress dialog with Cancel is shown instead, and cancelling keeps finished images.
- **Orientation:** Qt 6.12 applies EXIF orientation for JPEG and TIFF only. `io` parses PNG `eXIf` and WebP `EXIF` orientation itself. `QImageReader` handlers are order-sensitive, so the frame/page count uses a separate reader.
- **Image cache:** levels with longest side 256…8192 px (capped at the asset size), stored as `QPixmap` in a `QCache` with KiB cost and a 512 MiB default budget. A miss returns the closest cached level and schedules the needed one (at most 64 pending). Decodes read bytes from the session store, so originals never stay in RAM.
- **View:** the scene rect is fixed to ±1e7 (spec range); at 6400% the scroll range (2e7 × 64) still fits an int. Zoom is clamped to 1%–6400% and anchored manually. Selection outlines and the rubber band are drawn in `drawForeground` in viewport coordinates, so they keep a constant on-screen width. This requires `FullViewportUpdate` (no scroll blitting). Items are selectable but not movable until M2.
- **Peak memory:** at most one raw decode above 16 MP runs at a time; `QImageReader`'s allocation limit is set from `Limits::maxDecodedPixels`.

## M2 implementation notes

- **One mutation primitive:** `BoardDocument::apply(DocumentChange)` does removals, insertions, updates (whole-record replacement by ID) and view settings. It validates the complete resulting state before committing and returns the exact inverse. `CommandController` stores `{forward, inverse}` in one generic `QUndoCommand`, so every edit kind undoes the same way. A randomized 300-step session test checks exactness (`tst_commandcontroller`).
- **Operations are pure:** `editing::ops::*` inspect the document and return a change. All rules live there: locks (including members of locked groups), flat groups, world-preserving group/ungroup, z-order, layout and paste ID remapping. Canvas and app code never build changes by hand.
- **Transforms:** `displaySize` stays natural; every resize, rotation and flip is in `transform` (group-local for members). Crop is a normalized image-local rectangle, so the visible part stays in place.
- **Gestures:** `TransformTool` previews by setting `QGraphicsItem` transforms. Release commits one `ops::transform` (or `ops::resizeNote`); Escape restores the preview. The document is untouched until commit.
- **Scene sync:** `BoardScene` applies each `DocumentDelta` in place: new groups, then removals (surviving members detached), then images and notes with their parents, then group order. Selection therefore survives undo.
- **Lifetime rules found by the sanitizer and debug-assert runs:**
  - `MainWindow` disconnects child signals in its destructor.
  - The view tracks the note being edited by ID, because the scene may delete the item first.
  - Undo/redo actions leave text and crop modes before acting.
- **Clipboard:** `application/x-glacierref-items` carries a CBOR fragment with asset bytes. Decoding treats it as untrusted (types, finite numbers, canonical IDs, limits, SHA-256). Paste stores the bytes in the session store and remaps IDs.

## M3 implementation notes

- **Format:** [FORMAT.md](FORMAT.md) is the contract; committed fixtures (`tests/fixtures/boards`) load on every CI platform and must never be regenerated for schema 1. The manifest decoder is strict and validates the whole board (`BoardDocument::validateSnapshot`) before anything uses it; JSON depth is pre-scanned before parsing.
- **Loading:** a worker opens the file, checks the archive (entry names, count, uncompressed total, per-entry compression ratio), decodes the manifest, then streams each embedded asset into the session store after verifying length, SHA-256, detected format and dimensions. The file is closed before the result reaches the GUI, which creates the document in one `apply()` and starts with an empty undo stack.
- **Saving:** the GUI thread takes an immutable snapshot (`snapshotAll`, metadata, view, a 512 px preview rendered from the scene) and the history marker; a worker writes the archive through miniz callbacks into a `QSaveFile` without direct-write fallback. `CommandController` tracks clean state by the marker of the command at the top of the stack, so a save that completes after further edits leaves them dirty. `ZipWriter` uses a tracking allocator because miniz 3.1.2 leaks its deflate state on failed writes.
- **Locks and conflicts:** `DocumentLock` is a sibling `QLockFile` that never expires by age; Qt takes over locks of ended same-host processes, other-host locks need an explicit, confirmed removal. Size and modification time at load/save detect external modification before overwriting.
- **Links:** in memory a linked asset has an absolute, normalized path (`link_paths`, the same syntax on every platform), and its bytes are also put into the session store, so decoding and the cache don't distinguish link from embed. Each save recomputes stored paths relative to the target. Link repair (`ops::replaceAssets`) swaps immutable asset records in one change; link states (available, missing, changed) are window state, shown as labelled placeholders per asset ID, so undo shows the right placeholder.
- **Recovery:** `RecoveryStore` keeps snapshots per open board (window), assets in a SHA-256 pool shared by all sessions (hard links from the session store where possible). Discovery only looks at sessions whose lock is free. A recovered board is written as the new window's own snapshot before the old data is removed, so a second crash still finds it.
- **Text editing:** `BoardView` lets a list of shortcuts through while a note is edited (Save, Save As, Open, New Window, Close Window, Quit); their handlers commit the text first.

## M4 implementation notes

- **Settings:** `SettingsService` (app) is the only place that reads and writes QSettings keys for preferences; windows follow its signals (`shortcutsChanged`, `appearanceChanged`, `cacheBudgetChanged`, `recoveryChanged`, `menuBarVisibleChanged`), so all windows change together. Shortcut overrides store only differences from the defaults that `MainWindow::addAction` registers. Tests give every process its own INI settings (`GlacierTestMain.h`).
- **Diagnostics policy:** logs never contain note text or image bytes; messages pass through `diagnostics::redact` (home directory → `~`, user name → `<user>`), and file events name files, not paths. The handler writes to stderr and to `<AppData>/logs/glacier-ref-<UTC time>-<pid>.log` (1 MiB cap per file, newest five kept). Categories are `glacier.app`, `glacier.files`, `glacier.recovery`, `glacier.import` (info and above by default).
- **Capabilities:** `platform::PlatformWindowService` decides per platform plugin what can be promised (opacity, confirmable always-on-top, system move/resize, positioning). UI never claims more: Wayland gets "requested — your compositor decides" and no opacity menu.
- **Frameless:** `FramelessController` filters the view and viewport events; it only calls `startSystemMove/Resize` from a genuine press, never moves windows by coordinates, and emits `unsupported` so the window restores its frame.
- **Progress dialogs:** never delete a `QProgressDialog` from a slot that may run inside its own `setValue()` (window-modal dialogs process events there); `retireProgressDialog` disconnects, cancels and `deleteLater`s.
- **Bulk selection:** select through `BoardScene::selectIds`/`selectAllSelectable`, which emit `selectionChanged` once; per-item emissions made listeners quadratic.

## Ownership

- RAII everywhere. No raw owning pointers.
- QObjects are owned by their QObject parent (widgets by their parent widget,
  the scene by its view). Top-level windows use `Qt::WA_DeleteOnClose` or an
  owning `std::unique_ptr`.
- Non-QObject lifetime uses `std::unique_ptr` (sole owner) or
  `std::shared_ptr` (shared, e.g. immutable snapshots). Raw pointers and
  references are non-owning observers only.
- `QPointer` guards any observer that can outlive a QObject, especially across
  asynchronous boundaries.
- No global objects with non-trivial destructors (avoids static destruction
  order problems at shutdown).

## Model, view, and commands

- `BoardDocument` is the single authority for board state and the only input
  to serialization. Graphics items mirror it and are rebuilt or updated from
  model changes; they are never read back for saving.
- Every model mutation goes through a `QUndoCommand` on the document's
  `QUndoStack` (one stack per document/window). A completed gesture is one
  command; live gesture feedback is applied to the view and committed once on
  release. Escape discards an uncommitted gesture.
- Commands hold IDs and value data, never pointers to graphics items.
- Menu actions are the canonical action registry; shortcuts and context menus
  trigger the same `QAction`s.
- Pan and zoom never create commands or mark the document dirty (spec §3).

## Revisions and asynchronous work

- The document has a monotonically increasing **revision**, incremented on
  every committed mutation, and a **document token** (its `Id`) that changes
  when a window's document is replaced.
- Background jobs (decode, hash, save, recovery) receive immutable value
  inputs and return results tagged with `{documentToken, revision}`. The GUI
  thread discards results whose token no longer matches; for saves, only the
  revision that was saved becomes clean, never a newer one.
- Cancellation is cooperative (a shared atomic flag per job batch). Cancelling
  or closing a window never dereferences a destroyed view: results are
  delivered by queued signals to a receiver guarded by `QPointer` or the
  receiver's lifetime.

## Threads

- GUI thread only: all `QWidget`/`QGraphicsItem` access, `QPixmap` creation,
  `QUndoStack` mutation, dialogs.
- Worker threads (a bounded `QThreadPool` with Qt Concurrent) operate on
  `QByteArray`, `QImage`, and plain value types only.
- `QImage` crosses threads by value (implicitly shared); `QPixmap` is created
  from it on the GUI thread.

## Assets

- **Session asset store** (M1.09): embedded asset bytes live in a per-session
  directory under the application data directory (the portable data directory
  in portable mode), named by SHA-256. Opening a board streams and verifies
  its embedded assets into the store and closes the archive, so the board file
  is not held open (atomic replacement works on Windows).
- Decoders, saves, clipboard, and recovery read bytes from the store; RAM
  holds only decoded images in the bounded `ImageCache`.
- Assets are reference-counted by the board, undo history, and in-flight saves;
  undo commands reference assets by digest, never copy image bytes.
- Recovery uses a separate SHA-256 asset pool plus small manifest snapshots
  (spec §5); each session holds a `QLockFile` so only sessions of ended
  processes are offered for recovery.

## Import format allowlist

Qt loads every image plugin installed on the system. On KDE systems this
includes kimageformats (PSD, AVIF, HEIF, XCF, RAW, …), and other platforms
differ again. Glacier Ref therefore never treats "`QImageReader` can read it"
as "supported":

- The importer has an explicit allowlist: `png`, `jpeg`, `webp`, `bmp`, `gif`,
  `tiff`, and `svg` (the latter only after the SVG validation pass).
- The format is detected from content (`QImageReader::format()` without a
  hint) and must be on the allowlist; the file extension alone is never trusted.
- Anything else is rejected with a clear "unsupported format" error, even if a
  plugin could decode it.

## Persistence

- All file writes that replace user data go through `QSaveFile` (sibling
  temporary file plus atomic commit, direct-write fallback disabled for boards)
  or an equivalent public mechanism. The board format is in [FORMAT.md](FORMAT.md).
- Serialization is locale-independent (`QJsonDocument`, `QString::number`);
  tests run in a comma-decimal locale to enforce this.
- Archive I/O uses miniz through `QIODevice` callbacks only (`MINIZ_NO_STDIO`
  is set, so miniz cannot open paths itself).

## Platform abstraction

- Platform-specific behavior lives behind `PlatformWindowService` (M4), which
  reports capabilities (e.g. "always-on-top can be enforced", "window opacity
  supported") instead of assuming them. The UI hides or explains unsupported
  features; it never shows a state the platform did not confirm.
- Window moves and resizes use `QWindow::startSystemMove/Resize` from real
  input events; no global-coordinate polling.
- `PlatformInfo` (M0) reports the active Qt platform plugin, session, desktop,
  and screens for validation records (`glacier-ref --platform-info`).

## Identity

`io.github.cracksoldier.GlacierRef` is the Wayland app_id (via
`QGuiApplication::setDesktopFileName`), desktop-entry name, macOS bundle
identifier, and Flatpak ID. QSettings uses organization `GlacierRef`, domain
`cracksoldier.github.io`, application `GlacierRef`. Board files use MIME type
`application/x-glacierref` and suffix `.glacierref`.
