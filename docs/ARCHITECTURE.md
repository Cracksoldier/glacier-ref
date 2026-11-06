# Architecture and contracts

Milestone M0.08. This document fixes the rules later milestones build on. The
specification (§6) remains authoritative; this file makes its architecture
concrete and records decisions made during implementation.

## Components and build targets

| Directory | Target | Depends on | Responsibility |
| --- | --- | --- | --- |
| `core/` | `glacier::core` | Qt Core | Data model (`BoardDocument`, M1), IDs, validation, revisions, app identity. **No Gui/Widgets.** |
| `io/` | `glacier::io` (M3) | core, miniz | `BoardSerializer`: ZIP/JSON validation, migrations, atomic save; `RecoveryService` storage |
| `canvas/` | `glacier::canvas` | Qt Widgets | `BoardScene`/`BoardView`, `ImageItem`, `NoteItem`, `GroupItem`: presentation and hit testing only |
| `platform/` | `glacier::platform` | Qt Gui | `PlatformInfo` (M0); `PlatformWindowService` capabilities (M4) |
| `app/` | `glacier::app`, `glacier-ref` | all | Windows, menus/actions, `CommandController`, `SettingsService`, wiring |
| `third_party/miniz` | `glacier::miniz` | — | Vendored ZIP/deflate (see [DEPENDENCIES.md](DEPENDENCIES.md)) |
| `tests/` | `tst_*` | as needed | Qt Test suites, each run in default and `de_AT` locale |

Dependency direction: `app → canvas, io, platform → core`. `core` never
includes Gui or Widgets headers; `canvas` items never own persistent state.

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
  temporary file plus atomic commit) or an equivalent public mechanism.
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
