# Glacier Ref — Implementation Milestones

Version: 1.1  
Date: 2026-11-06  
Source: `GLACIER_REF_SPECIFICATION.md`, version 1.1  
Status: M0 and M1 complete; M2 not started

## 1. Purpose and working rules

This document turns the Glacier Ref specification into actionable development stages. The specification remains authoritative for behavior, architecture, limits, and acceptance scenarios A01–A12. Milestone completion requires working functionality and recorded validation, not merely merged code.

The target is a focused offline reference-board application using C++20, Qt 6 Widgets, QGraphicsView, and CMake. CachyOS under native Wayland is the primary development platform. Linux and Windows are the primary release platforms; macOS has build coverage and secondary release validation. Original application source is MIT-licensed.

- Keep each milestone demonstrable and its changes reviewable.
- Use stable task IDs below in issues, commits, or pull requests.
- Mark a task complete only when its associated behavior has been verified.
- Record the commit, environment, test results, and remaining limitations when closing a milestone.
- Do not add cloud services, accounts, telemetry, URL downloads, or background network access.
- Do not replace CMake or the Widgets architecture without revising the specification.
- Validate Wayland behavior early; automated virtual-display tests do not replace native desktop checks.
- Windows and macOS manual validation is performed by the maintainer on separate machines; record their OS versions and hardware in the completion record.
- No calendar deadlines are assumed. Estimate durations after M1 provides an implementation and performance baseline.

## 2. Roadmap

| Milestone | Outcome | Prerequisites | Acceptance ownership | Status |
| --- | --- | --- | --- | --- |
| M0 — Foundation | Reproducible cross-platform application skeleton | None | Build prerequisites for all gates | Complete |
| M1 — Canvas and import | Usable image board with bounded decoding | M0 | Early A01, A08, A10, A11 checks | Complete |
| M2 — Editing | Complete v1 board editing and undo/redo | M1 | A01–A02 | Not started |
| M3 — Durable boards | Portable saving, links, recovery, safe loading | M2 | A03–A07 | Not started |
| M4 — Desktop polish | Reliable minimal UI, native behavior, performance | M3 | A08, A10, A11 | Not started |
| M5 — Distribution | Tested installer, portable ZIP, AppImage, Flatpak, macOS build | M4 | A09, A12 | Not started |
| M6 — Release candidate | Validated, documented v1 candidate | M0–M5 | Full A01–A12 regression | Not started |

Packaging prototypes may start after M0, and file-format design may start during M1. Their milestones still close only after their stated prerequisites and gates are satisfied. Early groundwork must not postpone data integrity, recovery, or native Wayland validation.

## 3. M0 — Foundation

**Goal:** Make Glacier Ref build and launch reproducibly before implementing board features.

### Tasks

- [x] **M0.01 — Repository skeleton:** Create `app/`, `core/`, `canvas/`, `io/`, `platform/`, `tests/`, `resources/`, `packaging/`, `cmake/`, and `docs/` with a top-level `CMakeLists.txt` (CMake 3.21+) using `add_subdirectory`, plus `CMakePresets.json` for Debug, Release, and sanitizer builds.
- [x] **M0.02 — Toolchain:** Pin a publicly obtainable Qt 6 patch in one place shared by all workflows, with matching compilers, build configurations, and architecture targets. Install it in CI through a commit-pinned installer action. Add a scheduled workflow that proposes Qt bumps when a newer stable Qt 6 release appears. Declare the minimum supported Qt in `find_package`. Record CachyOS development versions separately from reproducible CI pins.
- [x] **M0.03 — Build boundaries:** Separate app, reusable core-library, and Qt Test targets; register tests with CTest. Set C++20, warnings, and sanitizer options consistently through shared modules in `cmake/`.
- [x] **M0.04 — Dependencies:** Select and pin a maintained ZIP library with compatible licensing and CMake integration that provides deflate without Qt's private zlib, writes through `QIODevice` callbacks, and streams single entries (candidates: miniz, minizip-ng), vendored or fetched with `FetchContent` pinned to an exact commit hash. Pin packaging tools to exact versions. Record all Qt modules (including the Qt Image Formats runtime plugins) and third-party dependencies; use no private Qt APIs or Qt Network dependency.
- [x] **M0.05 — Application shell:** Launch a QWidget-based main window with an empty QGraphicsView, normal window decorations, application identity (`io.github.cracksoldier.GlacierRef` as Wayland app_id, desktop-file name, and settings identity), and safe shutdown. Wrap all user-visible strings in `tr()` from the start; the v1 UI is English only.
- [x] **M0.06 — CI:** Add GitHub Actions build/test jobs for Linux, Windows, and macOS, running core tests under both an English and a comma-decimal locale (for example `de_AT.UTF-8`). Pin third-party actions, minimize permissions, and keep secrets out of untrusted PR jobs.
- [x] **M0.07 — Project documentation:** Add MIT license, README, initial build instructions, CONTRIBUTING, and dependency inventory.
- [x] **M0.08 — Architecture contracts:** Document ownership, GUI/worker thread boundaries, document revisions, command handling, the per-session asset store, and platform abstractions.
- [x] **M0.09 — Offline audit procedure:** Define a repeatable network audit (for example a network namespace without connectivity plus tracing of socket connect calls) for use from M1 onward.

### Deliverables

A buildable application shell, CMake project and presets, CI workflows, dependency inventory, and platform build instructions.

### Completion criteria

- [x] A clean checkout builds in Debug and Release on the primary development platform.
- [x] Linux, Windows, and macOS CI jobs compile and run the initial meaningful core tests.
- [x] The app launches using native Wayland on CachyOS; the test record identifies the Qt platform backend and compositor.
- [x] The ZIP-library choice and exact CI toolchain pins are recorded.

**Demo:** Build from a clean checkout, launch the app on CachyOS Wayland, resize it, and close it normally.

## 4. M1 — Canvas and import

**Goal:** Import local reference images into a responsive, navigable board.

### Tasks

- [x] **M1.01 — Document model:** Implement `BoardDocument` with stable IDs, item/asset references, document revisions, and validation independent of graphics items.
- [x] **M1.02 — Canvas:** Implement pan, cursor-anchored zoom, fit board/selection, an expanding scene region, coordinate limits, and the specified zoom range.
- [x] **M1.03 — Selection:** Implement single selection, additive selection, rectangle selection, and clear visual selection feedback.
- [x] **M1.04 — Import entry points:** Support file dialogs, local-file drag-and-drop, local `file:` URLs, and clipboard image pixels. Avoid duplicate imports from redundant MIME representations.
- [x] **M1.05 — Offline boundary:** Reject HTTP(S)-only input with useful guidance; do not fetch HTML image sources or remote dependencies.
- [x] **M1.06 — Raster formats:** Decode PNG, JPEG, WebP, BMP, GIF, and TIFF. Preserve alpha and source bytes; apply EXIF orientation and supported sRGB conversion consistently, with fixtures proving orientation for JPEG, WebP, PNG, and TIFF. Encode clipboard pixels as PNG.
- [x] **M1.07 — SVG policy:** Implement the `QXmlStreamReader` validation pass: element/attribute whitelist, ignored editor metadata, rejection of scripts, `foreignObject`, event attributes, DTD/entities, `@import`, and any reference other than `#id` or bounded PNG/JPEG `data:` URIs. Re-render SVG vectors per zoom level through the cache. Verify that SVG processing causes no external I/O.
- [x] **M1.08 — Worker pipeline:** Add bounded decoding/hashing jobs, progress, cancellation, stale-result rejection, and safe document closure while jobs run.
- [x] **M1.09 — Asset store/cache:** Implement the per-session asset store in the application data directory, organized and deduplicated by SHA-256, with free-space checks and cleanup of stores left by ended processes; prototype resolution levels and a bounded LRU decoded-image cache. Keep QPixmap/GUI operations on the GUI thread.
- [x] **M1.10 — Import resilience:** Enforce source-size and dimension safeguards before large allocation. Report failed files individually. Handle animated input as a static first frame and multi-page TIFF as its first page, each with a notice.
- [x] **M1.11 — Baseline fixtures:** Prepare licensed or generated standard/stress datasets and malformed-input fixtures. Script the constrained measurement profile (four P-cores, 16 GiB memory, 2560×1440 display) on the reference workstation and record the hardware and initial timing/memory results.
- [x] **M1.12 — Wayland feasibility:** Exercise native clipboard, file drop, focus, dialogs, scaling, move/resize, and always-on-top capability behavior.

### Deliverables

A usable import-and-navigation prototype, reproducible datasets, import error handling, and a native Wayland feasibility report.

### Completion criteria

- [x] All specified static formats display with correct orientation and transparency.
- [x] Imports remain cancellable; closing a board during decoding causes no crash or stale insertion into another board.
- [x] Unsupported inputs leave the board usable and do not produce network requests.
- [x] The standard dataset can be navigated without retaining every original image as a full-resolution decode.
- [x] Performance results and compositor limitations are recorded, with corrective work assigned to M4.

**Demo:** Drop a mixed image batch, paste an image, pan/zoom, select items, cancel a large import, and show the rejection of a URL-only drop.

## 5. M2 — Editing

**Goal:** Complete the editing workflow before making the file format durable.

### Tasks

- [ ] **M2.01 — Action registry:** Route menus, context menus, and shortcuts through common actions with consistent enable/disable behavior.
- [ ] **M2.02 — Undo foundation:** Implement a per-document QUndoStack with a bounded, configurable limit (default 200 steps), asset reference lifetimes, dirty state, and command coalescing. Include import and deletion.
- [ ] **M2.03 — Transforms:** Add move, proportional/non-proportional resize, free/snapped rotation, horizontal/vertical flip, and multi-selection transforms (including flips) around the selection bounds.
- [ ] **M2.04 — Gestures:** Make one completed gesture one undo step; Escape cancels without committing. Canvas navigation must not modify item geometry.
- [ ] **M2.05 — Crop:** Add image-local non-destructive crop; preserve original bytes and correct behavior under rotation, flip, and grouping.
- [ ] **M2.06 — Groups:** Implement flat named groups, enter/leave group editing, and group/ungroup with world-transform preservation. Reject nested grouping and grouping of selections that contain locked items.
- [ ] **M2.07 — Locks and stacking:** Add undoable lock/unlock and z-order commands. Locked groups protect children; children cannot be locked individually; locked objects remain selectable for inspection/unlocking.
- [ ] **M2.08 — Layout:** Implement alignment and deterministic row arrangement (reading-order sort, square-root target row width) using transformed bounds, configurable spacing, and the specified treatment of groups/locked items.
- [ ] **M2.09 — Notes:** Add Unicode plain-text notes with wrapping, resize-to-reflow, rotation, font size, foreground/background styling, and sensible undo coalescing.
- [ ] **M2.10 — Preview:** Add board background and grayscale preview without modifying image bytes; both are undoable and mark the board dirty, whereas pan/zoom never does.
- [ ] **M2.11 — Multi-window ownership:** Give each window its own board, undo history, selection, and worker-result routing. New/Open create windows in the current process; each launch is an independent process without single-instance forwarding.
- [ ] **M2.12 — Clipboard:** Implement Copy, Cut, Paste, and Duplicate of items within and between windows and processes using `application/x-glacierref-items`, plus Copy Image (crop and flip applied) to the system clipboard.
- [ ] **M2.13 — Behavioral tests:** Verify transform/crop math, grouping invariants, lock behavior, command reversibility, clipboard round-trips, and text-editing shortcut priority.

### Deliverables

The complete v1 editing toolset and meaningful document/command tests.

### Completion criteria

- [ ] **A01 passes:** All supported import paths/formats work offline and fail gracefully.
- [ ] **A02 passes:** Editing operations undo/redo to the expected document state.
- [ ] Grouping and ungrouping transformed/cropped items does not visually move them.
- [ ] Locked items cannot be modified or removed through editing/layout commands.
- [ ] Notes can be edited without canvas shortcuts intercepting text input.

**Demo:** Build a reference board with images and notes, crop and rotate items, group them, arrange selections, lock an item, copy items into a second window, then undo and redo the workflow.

## 6. M3 — Durable boards

**Goal:** Make boards portable, recoverable, and safe to save and load.

### Tasks

- [ ] **M3.01 — Format contract:** Document schema version 1, manifest fields, archive entries, IDs, timestamps, transform coefficients/order, orientation/crop semantics, and compatibility policy.
- [ ] **M3.02 — Embedded persistence:** Write/read `.glacierref` ZIP files with `manifest.json`, deduplicated assets, and optional preview. Preserve all required board state.
- [ ] **M3.03 — Validation:** Validate manifests and assets before replacing the active document. Reject invalid IDs/references/transforms, group cycles, nested groups, unsupported versions, traversal, and bad digests.
- [ ] **M3.04 — Resource limits:** Bound archive entries, decompressed bytes, compression ratios, JSON depth, items, notes, asset sizes, and image dimensions.
- [ ] **M3.05 — Atomic saves:** Save immutable document snapshots through `QSaveFile` (sibling temporary file) and replace targets only after successful completion. Keep newer edits dirty after asynchronous saves.
- [ ] **M3.06 — Save conflicts:** Detect external modification, support Save As/explicit replacement, and prevent concurrent writable access using sibling `QLockFile` document locks. Open locked or unlockable boards read-only (in-memory edits allowed, Save disabled, Save As offered). Handle stale locks conservatively.
- [ ] **M3.07 — Linked assets:** Add explicit linked import through “Import as Link…”, relative/absolute path handling, first-save resolution, Save As rebasing, and fingerprint comparison.
- [ ] **M3.08 — Link repair:** Show stable placeholders for missing assets and implement Locate, Reload, and Embed. Do not silently accept modified linked content.
- [ ] **M3.09 — Recovery:** Implement the SHA-256 recovery asset pool plus small manifest snapshots at the configurable default 60-second interval, including unnamed boards. Retain three snapshots per document within a bounded budget, removing pool assets that no retained snapshot references. Use per-session `QLockFile`s so only sessions of ended processes are offered for recovery.
- [ ] **M3.10 — Startup/close flows:** Offer recovery choices; reopen recovery as dirty; implement Save/Discard/Cancel and safe recovery cleanup after normal closure.
- [ ] **M3.11 — Compatibility fixtures:** Add format round-trip and migration infrastructure tests, including explicit rejection of unsupported newer schemas.
- [ ] **M3.12 — Failure injection:** Exercise interrupted writes, disk-full behavior, permissions errors, external modification, locked boards, and termination during recovery/save.

### Deliverables

Documented file format, serializer, linked-asset management, recovery UI, and persistence/failure fixtures.

### Completion criteria

- [ ] **A03 passes:** Embedded boards round-trip and move between Linux and Windows without their source images.
- [ ] **A04 passes:** Linked assets rebase correctly and missing/changed links can be repaired or embedded.
- [ ] **A05 passes:** Failed/interrupted saves retain the previous valid board; asynchronous saves preserve dirty-state accuracy.
- [ ] **A06 passes:** Forced termination yields the latest completed recovery for named and unnamed boards.
- [ ] **A07 passes:** Malformed/oversized/unsupported content fails safely and without external I/O.

**Demo:** Save a board, move it to another OS, reopen it, repair a missing link, then force termination and recover unsaved edits.

## 7. M4 — Desktop polish

**Goal:** Deliver the intended minimal workflow with reliable platform behavior and measured performance.

### Tasks

- [ ] **M4.01 — Minimal UI:** Finish toggleable menus, context menus, discoverable menu access, first-run guidance, Escape behavior, and Help (offline content plus a user-initiated repository link). Ensure window controls remain accessible.
- [ ] **M4.02 — Preferences:** Implement appearance, the default shortcut table and its customization/conflict reporting, recovery interval, and cache budget using SettingsService.
- [ ] **M4.03 — Window integration:** (M1.12 finding: KWin ignores the always-on-top hint on Wayland; M1 shows guidance only.) Implement PlatformWindowService capability handling, system move/resize, size/state restoration, honest always-on-top status/fallback guidance without compositor-specific integration, and window opacity where supported (hidden on native Wayland).
- [ ] **M4.04 — Optional frameless decision:** Enable only after validated native move/resize and recovery from hidden controls; otherwise retain standard decorations and record the decision.
- [ ] **M4.05 — Desktop validation:** Test CachyOS native Wayland with the recorded reference compositor, plus X11 and a secondary compositor smoke test on the maintainer's Fedora system.
- [ ] **M4.06 — DPI and input:** Check fractional scaling, mixed-DPI monitors, trackpads, focus, dialogs, Unicode paths, and text/menu shortcut conflicts.
- [ ] **M4.07 — Performance:** Optimize visible-item rendering, cache eviction, worker concurrency, decoding, and save progress against recorded fixtures.
- [ ] **M4.08 — Offline audit:** Monitor application-originated network activity across startup, help, imports, SVG processing, editing, recovery, and saves.
- [ ] **M4.09 — Diagnostics:** Provide useful local errors and avoid image bytes, note text, and full personal paths in default logs.

### Performance completion checklist

Measure on the specification’s reference workstation under the constrained profile (four P-cores, 16 GiB memory, 2560×1440 display) with the standard dataset: 300 distinct 2048×2048 JPEG images plus 50 notes.

- [ ] Warm navigation targets 60 FPS with 95th-percentile frame time no greater than 33 ms.
- [ ] Cached selection/transform feedback is under 100 ms.
- [ ] Initial cold content appears within 3 seconds; board is usable within 10 seconds.
- [ ] Steady-state memory is at most the 1.5 GiB target with the default 512 MiB decoded cache.
- [ ] Imports/saves avoid sustained GUI stalls above 200 ms.
- [ ] The 1,000-asset stress fixture retains responsive cancellation and bounded memory.

### Completion criteria

- [ ] **A08 passes:** Native Wayland controls, clipboard/drop, scaling, and menu restoration work; compositor constraints are communicated accurately.
- [ ] **A10 passes:** No application-originated network requests occur in audited workflows.
- [ ] **A11 passes:** Performance budgets are supported by repeatable measurements.
- [ ] All performance deviations are fixed or reflected in an explicit specification revision; they are not silently marked passed.

**Demo:** Use Glacier Ref beside a creative application on CachyOS Wayland, toggle menus, change shortcuts, move between displays, and navigate the standard dataset.

## 8. M5 — Distribution

**Goal:** Turn validated builds into reproducible, installable artifacts.

### Tasks

- [ ] **M5.01 — Release workflow:** Add version-tag packaging after successful tests, version/OS/architecture artifact names, checksums, and draft-release uploads.
- [ ] **M5.02 — Windows staging:** Build with the pinned MSVC/Qt pairing; run `windeployqt`; verify runtime dependencies and image plugins from a clean environment.
- [ ] **M5.03 — Windows installer:** Implement the Inno Setup script with per-user defaults, uninstall entry, optional file association, and upgrade handling.
- [ ] **M5.04 — Windows portable:** Create the ZIP from the same staging tree with its portable marker. Keep preferences/recovery in the writable sibling data directory and handle unwritable locations explicitly.
- [ ] **M5.05 — Linux AppImage:** Produce an AppImage from a documented baseline environment. Validate on CachyOS and Fedora, including native Wayland.
- [ ] **M5.06 — Linux Flatpak:** Add a Flatpak manifest on a pinned KDE runtime with no network permission and `--filesystem=home`, exporting the desktop entry, icon, and MIME type. Build a `.flatpak` bundle in CI and record the runtime's Qt version. Flathub submission is a separate maintainer step.
- [ ] **M5.07 — macOS package:** Build the initial Apple Silicon target, stage with `macdeployqt`, and produce an app archive or validated DMG.
- [ ] **M5.08 — Support labels:** Record macOS manual validation and signing/notarization status; label packages experimental until the required manual acceptance is complete.
- [ ] **M5.09 — Notices:** Include dependency notices, source links, release notes, and the documented licenses/obligations of actual bundled components.
- [ ] **M5.10 — Distribution checks:** Test clean-machine launch, import codecs, install, upgrade, uninstall, portable relocation, and profile/portable settings separation.
- [ ] **M5.11 — Maintainer instructions:** Document release reproduction, manual publication, signing credentials if available, and manual application updates.

### Deliverables

GitHub Actions packaging workflows; Windows installer and portable ZIP; Linux AppImage and Flatpak; macOS package with accurate support status; release checklist.

### Completion criteria

- [ ] **A09 passes:** Windows packages run without developer Qt on PATH; portable settings/recovery remain in the intended directory.
- [ ] **A12 passes:** macOS builds and passes core tests; package support claims match manual validation.
- [ ] The AppImage and Flatpak pass import/save/reopen and native Wayland smoke checks on the stated Linux matrix.
- [ ] CI produces checksums, notices, and draft artifacts without exposing release secrets to PR builds.
- [ ] Unsigned/unnotarized artifacts are identified accurately; credentials are not fabricated or embedded in the repository.

**Demo:** Download CI artifacts onto clean systems, run each package, import a WebP and SVG, edit and save a board, and reopen it.

## 9. M6 — v1 release candidate

**Goal:** Produce a reviewable release candidate with evidence that the complete workflow works.

### Tasks

- [ ] **M6.01 — Freeze scope:** Confirm all required v1 tasks are complete; keep deferred features outside the release candidate.
- [ ] **M6.02 — Regression:** Run A01–A12 against the candidate commit and packaged binaries, recording environment and evidence for each gate.
- [ ] **M6.03 — Portability drill:** Exchange embedded boards between Linux and Windows, including Unicode notes/paths, cropped/rotated images, groups, and backgrounds.
- [ ] **M6.04 — Recovery drill:** Repeat interrupted-save, forced-termination, and recovery tests against packaged builds.
- [ ] **M6.05 — Documentation:** Finalize README, build guide, architecture, schema, shortcuts, recovery guide, Wayland limitations, contributor guide, and release checklist.
- [ ] **M6.06 — Known limitations:** List compositor restrictions (always-on-top, window opacity on Wayland), macOS validation status, supported input formats, linked-file portability, Flatpak filesystem scope, and lack of `.pur` compatibility.
- [ ] **M6.07 — Candidate artifacts:** Produce versioned packages, checksums, release notes, and a draft release tied to the exact validated commit.
- [ ] **M6.08 — Maintainer review:** Record outstanding issues and release readiness. Publish only through the project’s explicit maintainer release step.

### Release blockers

Data loss or corruption; broken recovery; unsafe malformed-file handling; unintended network access; broken primary-platform import/edit/save workflows; missing required runtime dependencies; and unresolved acceptance failures block completion. A compositor’s unsupported always-on-top feature is not a blocker when the specified usable fallback and honest status are present. macOS limitations must remain accurately labeled under the specification’s secondary-support policy.

### Completion criteria

- [ ] All applicable acceptance gates pass with evidence attached to the candidate commit.
- [ ] No release-blocking issue remains open.
- [ ] User documentation matches the packaged behavior.
- [ ] The draft release contains the validated artifacts and known limitations.

**Demo:** Complete the full user journey on both primary platforms: import, organize, annotate, save, reopen, recover, and use the packaged application beside another tool.

## 10. Acceptance traceability

| Specification gate | Primary milestone | Evidence to retain |
| --- | --- | --- |
| A01 — Offline imports | M2 | Format/MIME matrix and failure fixtures |
| A02 — Editing and undo | M2 | Command/model tests and editing demo |
| A03 — Portable round-trip | M3 | Shared Linux/Windows board fixtures |
| A04 — Linked assets | M3 | Missing/changed/rebased link scenarios |
| A05 — Save integrity | M3 | Failure-injection and concurrent-edit results |
| A06 — Recovery | M3 | Named/unnamed crash-recovery results |
| A07 — Invalid content | M3 | Parser/resource-limit fixtures and sanitizer results |
| A08 — Wayland desktop | M4 | Native compositor/version checklist |
| A09 — Windows delivery | M5 | Clean-machine installer/portable results |
| A10 — Offline audit | M4 | Network audit procedure and captured results |
| A11 — Performance | M4 | Hardware, dataset, cold/warm timing and memory report |
| A12 — macOS support | M5 | CI results, manual checks, support label |

M6 reruns the full matrix. Earlier passing results must be revisited when subsequent changes affect their behavior.

## 11. Deferred scope

The following are not v1 milestones: animated playback, PDFs/videos, drawing annotations, color picking, presentation mode, plugins, Blender/Unreal integrations, rich-text/Markdown notes, nested groups, board image export, full color-proofing, dragging items out to other applications, click-through window mode, per-image opacity, AVIF/HEIC/PSD import, UI translations, and compositor-specific always-on-top integration. `.pur` compatibility needs a separate feasibility investigation before any commitment. Networking, cloud synchronization, and accounts remain excluded.

## 12. Milestone completion record

Copy this record into the issue or document used to close each milestone:

| Field | Value |
| --- | --- |
| Milestone | M0–M6 |
| Status | Not started / In progress / Blocked / Complete |
| Candidate commit | To be recorded |
| Completed task IDs | To be recorded |
| Acceptance gates and evidence | To be recorded |
| OS / compositor / Qt / compiler | To be recorded |
| Remaining limitations | To be recorded |
| Specification changes, if any | To be recorded |
| Completion date | To be recorded |

A completed milestone means its deliverables and exit criteria are satisfied. This planning document itself does not indicate that application implementation has started.
