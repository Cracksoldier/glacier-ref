# Glacier Ref — Product and Technical Specification

Version: 1.1 • Date: 2026-11-06 • Status: implementation baseline

Changes in 1.1: build system changed from qmake to CMake; Qt version pinning and update-tracking policy defined (§6); application identity, process model, and manual Windows/macOS validation confirmed (§1, §3, §7); review decisions added: session asset store and hash-addressed recovery (§4, §5), SVG policy (§4), view dirty-state rules, item clipboard, window opacity, English-only UI, help link, and editing defaults (§3), GIF/TIFF import (§4), Flatpak delivery (§7, §8), ZIP-library criteria and atomic-save mechanism (§5); performance reference machine and constrained measurement profile, Fedora as secondary Linux validation, and locale-independence testing (§7, §9); explicit import format allowlist (§4); M1 implementation decisions: folder import, svgz rejection, additional SVG rejections (§4).

## 1. Purpose

Glacier Ref is an independent, open-source desktop reference-board application inspired by PureRef’s minimal canvas workflow. It lets artists, 3D modelers, developers, and designers collect and arrange visual references beside their creative tools. It is implemented in C++20 with Qt 6 Widgets and CMake, with MIT-licensed application source.

The first release is a focused, polished reference-board tool. It does not promise complete PureRef feature parity. All normal operations work without an internet connection. Glacier Ref has no accounts, cloud synchronization, backend, telemetry, update polling, remote content fetching, or collaboration service.

### Confirmed decisions

| Area | Decision |
| --- | --- |
| Primary development environment | CachyOS, native Wayland |
| Primary release platforms | Linux and Windows |
| Secondary platform | macOS; portable architecture and build coverage, secondary release validation |
| Language and build | C++20, latest stable Qt 6.x (pinned in CI), CMake |
| UI | Qt Widgets and QGraphicsView; minimal canvas, toggleable menus |
| License | MIT for original Glacier Ref source |
| Windows delivery | Installer and portable ZIP, built through GitHub Actions |
| Linux delivery | AppImage and Flatpak, plus documented source build |
| Persistence | Documented `.glacierref` format; embedded assets by default, optional local links |
| Media | Static PNG, JPEG, WebP, BMP, GIF, TIFF, and SVG; AVIF, HEIC, and PSD planned for later versions |
| Network behavior | Offline only, including no URL downloads |
| Application identity | Reverse-DNS ID `io.github.cracksoldier.GlacierRef`; board MIME type `application/x-glacierref` |
| Process model | Every launch is an independent process; New/Open inside a process opens new windows; document locks handle conflicts |
| Windows and macOS validation | Manual, by the maintainer on separate Windows and macOS machines |
| Secondary Linux validation | Manual, by the maintainer on Fedora |
| Performance reference | Maintainer's CachyOS workstation, measured under a constrained 4-core/16 GiB profile (§9) |
| Always-on-top | Spec fallback only: window hints plus honest status and compositor guidance; no compositor-specific integration |
| Localization | English only in v1; translations later |
| Help | In-app offline help plus a user-initiated link to the project repository |

The remainder specifies engineering defaults where the user did not prescribe exact behavior. These defaults are concrete implementation choices, not claims of additional user decisions.

## 2. Scope

### Required for v1

- Effectively infinite canvas with pan, cursor-anchored zoom, fit board, and fit selection.
- Local image import through file dialogs, file drag-and-drop, and clipboard image data.
- Selection, movement, resizing, rotation, non-destructive crop, and horizontal/vertical flip.
- Multiple selection, alignment, deterministic automatic arrangement, grouping, locking, and stacking order.
- Plain-text notes, configurable board background, and non-destructive grayscale preview.
- Undo/redo, explicit saving, autosave recovery, and dirty-document prompts.
- Minimal window presentation, toggleable menus, configurable shortcuts, and best-effort always-on-top.
- Window opacity on platforms that support it; hidden on native Wayland.
- Copy, cut, paste, and duplicate of board items within and between windows; copying an image to the system clipboard.
- Portable native board files, optional linked local images, missing-file handling, and format documentation.
- Responsive operation with several hundred images and bounded decoded-image memory.

### Deferred

Animated playback, PDF/video import, drawing annotations, color picker, presentation mode, plugins, integrations with Blender/Unreal Engine, rich-text or Markdown notes, nested groups, board image export, and full color-proofing workflows are outside v1. Dragging items out to other applications, click-through window mode, per-image opacity, AVIF/HEIC/PSD import, UI translations, and compositor-specific always-on-top integration are planned for later versions. PureRef `.pur` compatibility is a separate investigation; v1 must not imply it can open or write that format. Cloud functionality and networking are excluded rather than scheduled future work.

## 3. User experience

### Main window

Each window owns one board and its independent undo history. Opening another board creates another window. Every launch of the executable is an independent process; there is no single-instance forwarding to a running process, because it would require Qt Network local sockets. Within a process, New and Open create new windows, and closing the last window exits that process. Per-document locks (§5) prevent conflicting writable access across processes, and recovery discovery must ignore sessions whose owning process is still running. The default view prioritizes the canvas, with no permanent sidebar. A small discoverable menu affordance, right-click menu, and keyboard shortcut expose commands. The menu bar can be toggled at any time; first-run guidance explains how to restore it. On macOS, native menu behavior may differ and must remain usable.

The app initially uses standard system window decorations for reliable Wayland behavior. Minimal mode hides application chrome; an optional frameless mode is enabled only after native move/resize behavior is validated. Escape exits temporary edit modes and can restore a usable minimal-mode state. No mode may leave the user unable to move, resize, close, or access commands.

Provide File, Edit, Board, View, and Help menus. Preferences include shortcuts, appearance, recovery interval, image-cache budget, and portable-mode information. Use a restrained dark Glacier appearance with a neutral default board background. Selection handles and focus indicators must remain visible against light and dark images.

Help provides offline in-app content (About, shortcut reference, and basic usage) and a link to the project repository, `https://github.com/Cracksoldier/glacier-ref`, which opens in the user's default browser only on explicit user action. The application itself makes no network request. Window opacity is adjustable where Qt can apply it (Windows, macOS, X11); on native Wayland the option is hidden. The v1 UI is English only; all user-visible strings use Qt's translation functions (`tr()`) from the start so translations can be added later.

### Default input map

| Action | Default |
| --- | --- |
| Select item | Left click |
| Add/remove selection | Shift + left click |
| Rectangle selection | Left drag on empty canvas |
| Move selection | Left drag selected item |
| Pan | Middle drag or Space + left drag |
| Zoom | Wheel centered at cursor; support trackpad input |
| Context menu | Right click |
| Fit board / selection | F / Shift+F |
| Save / Save As | Ctrl+S / Ctrl+Shift+S |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z; Ctrl+Y alternate on Windows/Linux |
| Group / ungroup | Ctrl+G / Ctrl+Shift+G |
| Toggle menu | Tab, except during text editing |
| Delete selection | Delete |
| Non-uniform resize / 15° rotation snap | Shift while dragging a resize / rotation handle |
| New window / Open / Close window / Quit | Ctrl+N / Ctrl+O / Ctrl+W / Ctrl+Q |
| Import images / Import as link | Ctrl+I / Ctrl+Shift+I |
| Copy / Cut / Paste / Duplicate | Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D |
| Copy image to system clipboard | Ctrl+Shift+C |
| Select all | Ctrl+A |
| New note | N |
| Crop mode | C |
| Flip horizontal / vertical | H / V |
| Lock / unlock | Ctrl+L |
| Raise / lower | Ctrl+Page Up / Ctrl+Page Down |
| Bring to front / send to back | Ctrl+Shift+Page Up / Ctrl+Shift+Page Down |
| Zoom in / out / 100% | Platform standard zoom (Ctrl++ / Ctrl+-) / Ctrl+0 |
| Grayscale preview | G |
| Always on top | Ctrl+T |
| Rename group | F2 |

Use platform-standard Command equivalents on macOS. Shortcuts are configurable and conflicts are reported. Text editing takes priority over canvas shortcuts. Defaults avoid keys that require AltGr on common non-US keyboard layouts. Menu commands are the canonical action registry; shortcuts and context menus invoke the same actions.

### Canvas and transforms

Use double-precision board coordinates and an expanding scene region rather than a literally unbounded allocation. Initial supported coordinate range is ±10 million logical units; reject non-finite coordinates. Zoom range defaults to 1%–6400%. Fit commands clamp to that range. Canvas zoom never modifies item dimensions.

Resizing preserves aspect ratio by default; a modifier permits non-uniform resizing. Rotation supports free rotation and 15-degree snapping. Multi-item transforms, including flips, operate around the selection bounds, so flipping a multi-selection mirrors the arrangement. Crop is edited in image-local coordinates, survives rotation/flip, and preserves original bytes. Escape cancels a gesture; each completed gesture is one undo action. Undo history is bounded (configurable, default 200 steps); assets referenced only by discarded history are released.

Alignment uses visible axis-aligned bounds after transforms. Auto-arrange packs selected top-level items into rows with configurable spacing, preserving their dimensions and treating groups as units. Items are ordered by reading order of their visible bounds (top edge, then left edge, then ID as tie-breaker); rows target a width of about the square root of the total item area including spacing, starting at the top-left corner of the arranged items' previous bounding box. If nothing is selected, it operates on all unlocked top-level items. Locked items remain stationary and are not obstacles in v1; document that arrangement may overlap them. A single undo restores the previous layout.

### Groups and locking

Groups have stable IDs, names, ordered children, and a shared affine transform. V1 groups cannot contain other groups. Grouping already-grouped items, or a selection containing a locked item, is rejected with an explanation. Group and ungroup operations preserve each child’s world transform. Double-click enters a group for child editing; Escape leaves it.

A locked item or group cannot be moved, cropped, transformed, deleted, or modified by arrangement. It can be selected for inspection and unlocked through the context menu. Group locks apply to children; children inside a group cannot be locked individually in v1, so the group is locked instead. Lock and unlock are undoable commands, and undo may restore prior states that include lock changes.

### Notes and preview

Notes support Unicode plain text, wrapping, font size, text color, and background color. Double-click enters editing; Escape leaves editing. Note edits are coalesced sensibly in undo history. Resizing a note changes its wrap width and reflows the text; its height follows the content, and font size changes only through the style setting. Notes can be rotated like other items. No HTML, scripts, or automatic external-link fetching are supported.

Grayscale is a board-wide view preference applied at rendering time. It never rewrites image assets. Background and grayscale preference are persisted with the board; transient selection and active editing state are not. Changing background or grayscale marks the board dirty and is undoable. Canvas center and zoom are saved with the next save, but they neither mark the board dirty nor enter undo history.

### Clipboard and duplication

Copy, Cut, Paste, and Duplicate operate on board items within a window, between windows, and between Glacier Ref processes. Item data travels through the system clipboard under the private MIME type `application/x-glacierref-items`, carrying an item fragment plus its embedded assets and bounded by the import limits. Paste adds items as one undoable batch, deduplicates assets by SHA-256, and places items at the cursor when it is over the canvas, otherwise at the view center. Pasted linked items stay linked; their paths are recalculated against the target board. Clipboard content in the private format takes precedence over image data from the same clipboard entry. Locked items can be copied, but Cut rejects them with an explanation.

Copy Image puts the selected image's pixels on the system clipboard (PNG) for other applications, with crop and flip applied at source resolution. Rotation and the grayscale preview are not applied. Dragging items out to other applications is deferred.

## 4. Import and asset handling

### Offline boundary

Accept local file paths, local `file:` URLs, and clipboard raster pixels. Browser drag-and-drop works only when the source supplies actual image data or accessible local files. An HTTP(S)-only drop or clipboard entry produces: “Online image links are not supported. Download the image or copy its image data first.” Never fetch HTML images, SVG dependencies, fonts, or remote URLs. The application has no Qt Network dependency.

### Supported formats

PNG/JPEG/WebP/BMP/GIF/TIFF decode through tested Qt image handlers; WebP and TIFF come from the Qt Image Formats module. SVG uses Qt SVG after enforcing the policy below. Every packaged release must ship and test required image plugins, particularly WebP and TIFF. Animated input (animated GIF, animated WebP, APNG) is imported as a static first frame with a visible notice; multi-page TIFF imports the first page with a visible notice. Animation is not silently promised. AVIF, HEIC, and PSD are planned for later versions. Imports use an explicit format allowlist checked against the format detected from file content, independent of which Qt image plugins are installed; extra system plugins (such as KDE's kimageformats) must not silently widen the accepted formats.

Apply EXIF orientation consistently before item-coordinate calculations. Qt applies EXIF orientation reliably for JPEG; verify WebP, PNG, and TIFF orientation with fixtures and add handling where Qt does not apply it. Preserve original file bytes for embedded imports; crop and transforms are separate metadata. Clipboard pixels are encoded as PNG. Convert supported color-profile data to sRGB for display; assume sRGB if absent. Preserve source bytes and alpha. Full monitor-specific proofing is deferred.

SVG acceptance must reject external resource references and active content. Support self-contained vector content and bounded embedded raster data only. A validation pass with `QXmlStreamReader` runs before Qt SVG sees the data:

- Allow a whitelist of SVG elements and attributes that Qt SVG renders.
- Ignore editor metadata: the `<metadata>` element and the sodipodi, inkscape, rdf, cc, and dc namespaces.
- Reject `<script>`, `<foreignObject>`, event-handler attributes (`on*`), DOCTYPE and entity declarations, processing instructions (such as `xml-stylesheet`), CSS `@import`, and CSS escapes (which could hide references).
- Compressed SVG (`.svgz`) is not supported in v1.
- Allow references (`href`, `xlink:href`, CSS `url()`) only to same-document fragments (`#id`) or `data:` URIs; reject all others.
- `data:` URIs may contain only PNG or JPEG; they are bounded by the per-asset size and dimension limits and validated like other raster input.
- Elements outside the whitelist produce a clear error naming the construct, rather than an empty or partially rendered successful import.

Parsing/rendering must never cause external I/O; add fixtures that prove this behavior. SVG items keep their vector source and are re-rendered through the image cache at the resolution levels the current zoom needs. Intrinsic size comes from `width`/`height` or `viewBox`, with a documented default when both are absent.

### Import transactions and limits

Decode and hash in bounded worker jobs. Show progress for batches and allow cancellation. Add successfully decoded items as one undoable batch; failures are listed by filename. Cancellation retains already accepted items and removes unfinished placeholders. Avoid duplicate import when MIME data exposes both pixels and a file for the same drag. A dropped or chosen folder imports the supported images directly inside it; subfolders are not traversed, and skipped subfolders and non-image files are reported.

Initial configurable safeguards: 100 MiB per input asset, 100 megapixels decoded dimensions, 2 GiB board file size, 10,000 items, and 100,000 characters per note. These are safety limits, not performance guarantees. Validate dimensions and allocation estimates before full decode. Unsupported or oversized input must not terminate the application.

### Embedded and linked assets

Embedded mode is the default and is always used for clipboard input. Duplicate embedded assets are deduplicated by SHA-256 of their stored bytes; independent board items may reference one asset.

During a session, embedded asset bytes live in a per-session store in the application data directory (the portable data directory in portable mode), organized by SHA-256. Opening a board streams and verifies its embedded assets into this store and then closes the archive, so the board file is not held open and atomic replacement also works on Windows. Decoders, saves, and clipboard operations read bytes from the store rather than keeping all originals in RAM. Insufficient free space is reported before opening or importing rather than failing midway. Normal closure removes the session's store; stores left by terminated processes are removed at startup once no live process owns them and no retained recovery snapshot needs them.

Linked mode is an explicit file-import choice through a separate “Import as Link…” command; drag-and-drop and paste always embed. Prefer a relative path from the board location; use an absolute path only when necessary and identify the portability consequence. Unsaved boards retain resolved absolute paths until first save. Save As recalculates relative paths to retain the same targets.

Missing links appear as labeled placeholders retaining position and dimensions. Offer Locate, Reload, and Embed actions. Never silently replace modified linked content: compare recorded fingerprints on load/reload, flag changes, and let the user accept them. No background filesystem watcher is required for v1. Embedded imports never need their original source paths after saving.

## 5. Persistence and recovery

### Native format

Use a single ZIP container with extension `.glacierref`. Use a maintained ZIP implementation with a compatible license and CMake integration; do not depend on private Qt ZIP APIs. The library must provide deflate without Qt's private zlib, write through callbacks to a `QIODevice` (so saves can use `QSaveFile`), and read single entries as streams. Candidates are miniz (MIT, includes deflate) and minizip-ng (zlib license, with vendored zlib). Pin the library to an exact version and commit before persistence implementation. Already compressed assets are stored without recompression.

| Entry | Content |
| --- | --- |
| `manifest.json` | UTF-8 document schema and asset index |
| `assets/<sha256>.<ext>` | Original embedded bytes or clipboard PNG |
| `preview.png` | Thumbnail, longest side at most 512 px; written by default, never required to load |

The manifest includes `format`, `schemaVersion`, `documentId`, `title`, creation/modification timestamps, `assets`, `items`, `groups`, and `view`. Format identifier is `glacier-ref`; initial schema version is 1. `title` is user-editable board metadata that defaults to the file base name at first save. IDs are UUIDs represented as strings. Timestamps are UTC ISO 8601. Numbers must be finite.

Each image item records ID, asset ID, z-order, affine transform, local display dimensions, normalized crop rectangle, and lock state. Each note records ID, text, geometry, style, transform, z-order, and lock state. Group members reference their group ID; child transforms are group-local. Store transforms as six documented affine coefficients with an explicit multiplication convention and round-trip tests. Define source orientation, crop, local scaling, group transform, and scene transform order in the schema documentation.

An embedded asset entry records digest, media type, archive entry, oriented dimensions, and byte length. A linked entry records path, path kind, digest at last accepted load, and dimensions. Do not store unnecessary absolute source paths for embedded assets. View data contains background, grayscale, canvas center, and zoom; window geometry remains a local preference.

### Read/write rules

Validate the entire manifest and required assets before replacing the active document. Reject duplicate IDs, cyclic or nested groups, missing asset references, invalid transforms, unsupported future schema versions, and path traversal. Bound entry counts, total uncompressed bytes, compression ratios, and JSON depth before allocation. Read archive entries without extracting arbitrary paths. Verify embedded digests. Corrupt boards produce an actionable error; optional previews may be ignored.

Write to a temporary sibling file, finish the archive, then atomically replace the target where the filesystem supports it, using `QSaveFile` or an equivalent public mechanism. Do not truncate the existing board before a successful save. Preserve the dirty state after failures. Save an immutable model snapshot asynchronously; if edits occur meanwhile, the completed save must not mark newer edits clean. Detect external modification before overwriting and offer Save As or explicit replacement.

Schema upgrades require explicit migration functions and fixture tests. Never silently rewrite a newer unsupported board. Record known limitations of atomic replacement on unusual filesystems.

### Recovery

Default recovery interval is 60 seconds when dirty, configurable and debounced. Recovery data lives in the application data directory and also covers unnamed boards. It uses an asset pool organized by SHA-256: each embedded asset is written once and shared by all snapshots and documents that reference it. Each snapshot is a small manifest that references pool assets. A snapshot counts as complete only after its manifest is atomically written and every referenced asset is present and verified. Recovery never overwrites the user’s saved board. Retain the newest three snapshots per document; pool assets no longer referenced by any retained snapshot are removed. Total recovery storage has a bounded budget, with least-recent cleanup that never removes the newest complete snapshot of an active document. Each running session holds a `QLockFile` in the application data directory, so recovery discovery offers only sessions whose owning process has ended.

At startup, offer Recover, Discard, or Open Original for interrupted sessions. Recovered documents open dirty and require explicit saving. Embedded assets are included; recovery cannot guarantee preservation of missing linked assets. Closing a dirty document offers Save, Discard, and Cancel. Successful normal closure removes that session’s stale recovery after the chosen action completes.

Use a per-document lock to prevent simultaneous writable windows/processes. The lock is a hidden sibling `QLockFile` next to the board (`.<board file name>.lock`), which also protects boards in shared network folders; document that it may appear in synced folders. When the board is locked elsewhere, or the lock cannot be created (for example in an unwritable directory), the board opens read-only: it may be edited in memory, Save is disabled, and Save As is offered. Stale lock recovery must be explicit and conservative.

## 6. Architecture

| Component | Responsibility |
| --- | --- |
| `BoardDocument` | Authoritative data model, IDs, validation, revisions, dirty state |
| `BoardScene` / `BoardView` | QGraphicsScene/QGraphicsView presentation and navigation |
| `ImageItem`, `NoteItem`, `GroupItem` | Rendering and hit testing; no persistence ownership |
| `CommandController` | QUndoStack commands, gesture coalescing, action availability |
| `AssetStore` | Per-session SHA-256 asset store, original bytes, hashes, linked resolution, reference lifetime |
| `ImportService` | MIME routing, validation, asynchronous decoding |
| `ImageCache` | Thumbnails, resolution levels, bounded LRU decoded cache |
| `BoardSerializer` | ZIP/JSON validation, migrations, atomic save |
| `RecoveryService` | Snapshot scheduling and recovery discovery |
| `PlatformWindowService` | Window capabilities and native integration |
| `SettingsService` | QSettings, shortcuts, local window preferences |

The data model is independent of graphics items and is the only serialization authority. GUI objects and QPixmap creation remain on the GUI thread; background workers operate on safe value data and QImage. Results carry document/revision tokens to discard stale work after closure or replacement. Cancellation must not dereference destroyed views.

Use RAII, explicit ownership, QObject parent ownership where applicable, and smart pointers for non-QObject lifetime. Avoid raw owning pointers. Asset bytes are shared; undo commands reference assets rather than copying full images. Assets remain alive while referenced by the board, an in-flight save, or undo history.

Start with raster viewport rendering. Do not require OpenGL for basic use. Add GPU acceleration only after profiling and preserve a reliable software path. Render visible items with appropriate resolution levels; original bytes must not imply retaining all full-resolution decodes in RAM.

Suggested repository: `app/`, `core/`, `canvas/`, `io/`, `platform/`, `tests/`, `resources/`, `packaging/`, `cmake/`, and `docs/`, with a top-level `CMakeLists.txt` that adds components through `add_subdirectory` and defines separate app, core-library, and test targets. Shared modules under `cmake/` own compiler settings, warnings, sanitizer options, and dependency declarations. Require CMake 3.21 or newer, use Qt's CMake API (`find_package(Qt6 …)`, `qt_standard_project_setup`, `qt_add_executable`), and provide `CMakePresets.json` for Debug, Release, and sanitizer configurations. Register Qt Test executables with CTest. Third-party sources are vendored or fetched with `FetchContent` pinned to an exact commit hash, never a branch or movable tag. Required Qt modules: Core, Gui, Widgets, Svg, Concurrent; Test for tests. Qt Image Formats is a required runtime plugin dependency (WebP, TIFF); GIF support comes from Qt's base image plugins. Use public APIs only. No QML or qmake build path is required.

Pin the latest stable, publicly obtainable Qt 6.x release and exact patch in CI at project bootstrap, defined in one place shared by all workflows; document the compiler and supported OS matrix with it. CI installs Qt from official Qt mirrors through a commit-pinned installer action (for example install-qt-action/aqtinstall). Do not let an unpinned latest Qt silently change release output. To track the latest stable Qt 6, a scheduled workflow compares the pin with the newest available release and, with minimal permissions, opens an issue or pull request proposing the bump; a bump is accepted only after normal CI passes. Local development builds, such as CachyOS with its system Qt, may use a newer Qt; `find_package` declares the minimum supported Qt version, and development versions are recorded separately from CI pins. Packaging tools (linuxdeploy and its Qt plugin, Inno Setup) are also pinned to exact versions rather than continuous or latest builds.

## 7. Platform behavior

### CachyOS and Wayland

Native Wayland is a release criterion, not an XWayland-only fallback. Test on CachyOS with KDE Plasma/KWin as the initial reference compositor; record exact versions. Secondary Linux validation is performed manually by the maintainer on Fedora; record its edition, version, and compositor, and use it as the secondary compositor smoke test when it differs from KWin. User desktop choice can expand this matrix later.

Window position, stacking, and decorations are compositor-controlled. Use Qt system move/resize requests from genuine input events. Do not implement window dragging with global coordinate polling or assume arbitrary top-level positioning works. Restore window size and state where possible; exact desktop coordinates are best-effort.

Always-on-top uses available window hints and platform capability handling. When enforcement is unavailable or cannot be confirmed, explain the compositor limitation and provide guidance to use its window rule/menu. Do not show “guaranteed pinned” state merely because a hint was requested. V1 uses no compositor-specific integration such as KWin scripting. A supported fallback is acceptable for release; broken core canvas input is not.

Test drag-and-drop, clipboard, dialogs, mixed-DPI monitors, fractional scaling, focus, menu restoration, and image decoding on native Wayland. Maintain an X11 smoke test as secondary coverage.

### Linux packaging

AppImage and Flatpak are both v1 Linux deliverables, alongside the documented source build. The Flatpak uses the app ID `io.github.cracksoldier.GlacierRef` and the KDE runtime. Qt then comes from the runtime rather than the CI Qt pin, so pin the exact runtime version in the manifest and record the Qt version it provides next to the CI pin. The sandbox grants no network permission, which also enforces the offline boundary. File dialogs use the desktop portal. Atomic sibling saves, document locks, and relative linked assets need directory access beyond what the document portal provides, so the v1 manifest requests home-directory filesystem access (`--filesystem=home`). Document this, and that linked files outside granted locations appear as missing. The Flatpak exports the desktop entry, icon, and `application/x-glacierref` MIME definition; the AppImage carries them without system integration. CI builds a `.flatpak` bundle as a release artifact; Flathub submission is a separate maintainer step.

### Windows

Target Windows 11 x64 initially. Produce an installer and a portable ZIP from the same staged release directory. Installer defaults to per-user installation, creates an uninstall entry, and offers optional file association. Portable mode is selected by a marker alongside the executable; preferences and recovery reside in a writable sibling data directory. If unwritable, explain the issue and offer normal profile mode instead of silently losing data.

Use an MSVC-compatible Qt kit and compiler pairing. Stage dependencies with `windeployqt`, then validate required image plugins and runtime components on a clean Windows environment without developer Qt on PATH. Manual Windows validation is performed by the maintainer on a separate Windows 11 machine; the clean-environment requirement applies to that machine. Code signing is desirable but requires separately provisioned credentials; do not make an unsigned build appear signed.

### macOS

Keep application code portable, provide a CMake build job and `.app` packaging using `macdeployqt`. Prefer Apple Silicon as the initial architecture; Intel/universal binaries are deferred unless added to the release matrix. Validate standard Command shortcuts, menus, file dialogs, and Retina rendering. Signing/notarization require release credentials. Manual macOS validation is performed by the maintainer on a separate macOS machine. Clearly label macOS packages experimental until manual acceptance is completed.

## 8. GitHub Actions and releases

PR workflows build and run Qt tests on Linux and Windows; include a macOS compile/test job. Linux automated GUI tests may use a virtual display, but they do not replace the manual CachyOS Wayland gate. Cache dependencies with keys containing OS, compiler, architecture, and exact Qt version. Pin third-party actions by commit and grant minimum permissions. Untrusted PRs receive no release secrets.

Version tags initiate packaging after tests pass. Windows packaging uses an Inno Setup script for the installer and archives the identical staging tree for portable distribution, adding the portable marker only to that variant. Linux packaging creates an AppImage in a documented baseline environment and a Flatpak bundle with a commit-pinned flatpak-builder action; test both on CachyOS and Fedora. macOS packaging creates an app archive or DMG according to validated tooling.

Release artifacts use version/OS/architecture names and include SHA-256 checksums, release notes, dependency notices, and source links. Upload to a draft GitHub release for maintainer publication. The project repository is `https://github.com/Cracksoldier/glacier-ref`; publishing releases remains an explicit maintainer step. Installation, upgrade, uninstall, and clean-machine launches are release checklist items. Application updates are manual downloads; the application does not check online.

## 9. Performance and quality targets

Targets are acceptance budgets, not measured claims. The reference machine is the maintainer's CachyOS workstation: Intel Core i9-13900K (8 P-cores + 16 E-cores, 32 threads), 64 GiB RAM, NVIDIA GeForce RTX 4090, SSD with btrfs, KDE Plasma 6.8.2 with KWin on Wayland, and displays of 2560×1440 at 144 Hz and 5120×1440 at 120 Hz. Because this is far above typical user hardware, budgets are measured under a constrained profile that approximates a contemporary 4-core, 16 GiB machine: the process is limited to four P-cores (8 hardware threads, for example `taskset -c 0-7` after confirming core numbering with `lscpu`) and 16 GiB memory (for example `systemd-run --user --scope -p MemoryMax=16G`), with the window on the 2560×1440 display. Unconstrained runs and the 5120×1440 display are recorded as supplementary data, not as acceptance evidence. Run fixtures from a filesystem with ample free space. Record OS, kernel, compositor, Qt version, GPU driver, the constraint settings, and hardware alongside results; establish the procedure in milestone 1.

Standard dataset: 300 distinct 2048×2048 JPEG images plus 50 notes. Stress dataset: 1,000 mixed assets; responsive cancellation and bounded memory are required, but no 60 FPS guarantee is imposed there.

- Warm-cache pan/zoom on the standard dataset: target 60 FPS, 95th-percentile frame time at most 33 ms.
- Cached selection/transform feedback: under 100 ms.
- Initial cold board content: within 3 seconds; standard board usable within 10 seconds on the reference SSD.
- Standard-dataset steady-state process memory: target at most 1.5 GiB; default decoded-cache budget 512 MiB.
- Imports and saves expose progress without sustained GUI stalls above 200 ms.

Tune implementation from measurements. Report disk-size and cold/warm distinctions. High-DPI correctness, Unicode paths, filenames with spaces, locale independence, and offline execution are acceptance requirements. Serialization and parsing must not depend on the system locale; run the test suite under a comma-decimal locale (for example `de_AT.UTF-8`) in addition to `C`/English. Avoid logging note content, image bytes, or full personal paths by default.

## 10. Verification and acceptance

| ID | Acceptance scenario |
| --- | --- |
| A01 | Import all supported static formats offline via files, drop, and clipboard; failures leave the app usable. |
| A02 | Pan/zoom, select, transform, crop, flip, group, align, arrange, and lock; undo/redo restores exact document state. |
| A03 | Save/reopen preserves assets, transforms, groups, notes, background, and view; embedded board moves between Linux and Windows without source files. |
| A04 | Linked files survive Save As path rebasing; missing/changed files show placeholders or warnings and can be relinked/embedded. |
| A05 | Disk-full or interrupted save preserves the previous valid board; concurrent edits remain dirty after background save. |
| A06 | Forced termination offers the latest completed recovery for both saved and unnamed boards. |
| A07 | Malformed JSON/ZIP/SVG, traversal, unsupported schema, excessive dimensions, and broken digests fail safely without network access. |
| A08 | Native Wayland window controls, clipboard, drag/drop, scaling, and menu restoration work; unsupported pinning is honestly reported. |
| A09 | Windows installer and portable ZIP run on a clean machine without Qt development tools; portable settings remain local to the portable directory. |
| A10 | Network monitoring shows no application-originated network requests during startup, import, editing, SVG rendering, saving, or help use; the user-initiated repository link opens in the external browser and is outside the application's own network activity. |
| A11 | Standard dataset meets measured performance budgets, with documented hardware and reproducible fixtures. |
| A12 | macOS builds and passes core tests; package support level matches completed manual validation. |

Use Qt Test for document operations, transform/crop mathematics, command history, linked paths, serialization/migrations, limits, and recovery. Add integration fixtures for external modifications, cancellation, and failing writes. Use a small set of GUI interaction tests plus explicit platform smoke checklists. Run sanitizer coverage on Linux for parsing and document lifecycle. Define the network-audit procedure in M0, for example running workflows in a network namespace without connectivity while tracing socket connect calls, and apply it from M1 onward. Build functional tests around outcomes rather than implementation details.

## 11. Implementation milestones

| Milestone | Deliverable and exit gate |
| --- | --- |
| M0 — Foundation | MIT repository layout, CMake project, pinned toolchain, CI builds on all three OS families, dependency inventory. Empty application launches natively on CachyOS Wayland. |
| M1 — Canvas and import | Model/view foundation, local import, decoding, pan/zoom, selection, cache prototype. Record performance baseline and confirm Wayland input and window constraints. |
| M2 — Editing | Transforms, crop, flip, notes, groups, locks, arrangement, grayscale, command registry, undo/redo. Pass A01–A02. |
| M3 — Durable boards | Documented ZIP/JSON schema, embedded/linked assets, Save As, atomic writes, recovery, locking, malformed-file validation. Pass A03–A07. |
| M4 — Desktop polish | Minimal UI, menu restoration, preferences, shortcuts, DPI/focus handling, platform window fallbacks, optimization. Pass A08 and A11; offline audit passes A10. |
| M5 — Distribution | GitHub Actions installer/portable ZIP/AppImage/Flatpak, dependency notices, macOS support labeling, clean-machine tests, release checklist. Pass A09 and A12. |
| M6 — v1 release candidate | Regression run, cross-platform board fixtures, recovery drills, user documentation, known limitations, draft release artifacts. All applicable gates pass. |

Do not defer persistence integrity or recovery until after public v1. Resolve ZIP-library selection and exact Qt/compiler pins during M0; validate canvas performance and compositor behavior before committing to optional frameless UI.

## 12. Open-source and documentation requirements

Original source uses the MIT license. Bundled Qt and third-party dependencies retain their own licenses; the MIT label does not relicense them. Prefer dynamic Qt linking and compatible dependencies. Before distribution, inventory the exact shipped modules, plugins, runtime libraries, and archive library, and fulfill their applicable notice/source/relinking obligations. Avoid private Qt APIs and dependencies that would unintentionally change the intended application-source license.

Required repository documentation: README, build instructions for each OS, CONTRIBUTING, LICENSE, third-party notices, architecture overview, native schema specification, shortcuts, recovery behavior, Wayland limitations, and release checklist. Use original branding and assets; describe inspiration without claiming PureRef affiliation.

## 13. Reference documentation

These references ground toolkit and deployment choices; requirements and engineering defaults above are specific to Glacier Ref.

- PureRef workflow overview: https://www.pureref.com/
- Qt CMake manual: https://doc.qt.io/qt-6/cmake-manual.html
- Qt QGraphicsView: https://doc.qt.io/qt-6/qgraphicsview.html
- Qt window behavior and Wayland limitations: https://doc.qt.io/qt-6/application-windows.html
- Qt QWindow system move/resize APIs: https://doc.qt.io/qt-6/qwindow.html
- Qt Windows deployment: https://doc.qt.io/qt-6/windows-deployment.html
- Qt licensing reference for release review: https://www.qt.io/licensing/open-source-lgpl-obligations
