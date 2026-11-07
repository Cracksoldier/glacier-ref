# Dependency inventory

Milestone M0.04. Update this file whenever a dependency, Qt module, or
runtime plugin is added, removed, or re-pinned. The binary-package inventory
(exact shipped files) is produced in milestone M5.

## Qt modules

| Module | Use | Linked by |
| --- | --- | --- |
| Qt Core | Base types, JSON, UUIDs, settings | all |
| Qt Gui | Images, painting, screens, platform integration | platform, canvas, app |
| Qt Widgets | Main window, QGraphicsView canvas | canvas, app |
| Qt Svg | SVG rendering (after the SVG validation pass, M1.07) | app (runtime), tests |
| Qt Concurrent | Bounded worker jobs for decoding/hashing (M1.08) | required by the spec; first used in M1 |
| Qt Test | Test suite only | tests |

Not used: **Qt Network** (forbidden by the offline requirement), QML/Quick,
Qt DBus (no direct use; Qt Gui may use D-Bus internally on Linux for portals
and accessibility, which is local IPC), private Qt APIs.

## Qt runtime plugins

| Plugin | Provided by | Purpose |
| --- | --- | --- |
| `platforms/wayland`, `xcb` | Qt Base (Qt Wayland client) | Linux windowing |
| `platforms/windows`, `cocoa` | Qt Base | Windows / macOS windowing |
| `platforms/offscreen` | Qt Base | Headless tests |
| `imageformats/qjpeg`, `qgif` | Qt Base | JPEG, GIF |
| `imageformats/qwebp`, `qtiff` | **Qt Image Formats** | WebP, TIFF |
| `imageformats/qsvg` | Qt Svg | SVG |

PNG and BMP decoding are built into Qt Gui. `tests/tst_imageplugins.cpp`
fails if any required format is missing.

Systems may have additional image plugins installed (e.g. KDE's
kimageformats: PSD, AVIF, HEIF, XCF, …). Glacier Ref must not rely on or
silently accept them: imports use an explicit format allowlist (spec §4,
[Architecture](ARCHITECTURE.md#import-format-allowlist)).

## Vendored third-party source

### miniz

| Field | Value |
| --- | --- |
| Version | 3.1.2 |
| Upstream | https://github.com/richgel999/miniz |
| Tag commit | `77d0dce8627735138c51770d1799a1ef48f2117d` |
| Source | Release asset `miniz-3.1.2.zip`, SHA-256 `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a` |
| Files | `miniz.c`, `miniz.h`, `LICENSE`, `readme.md`, `ChangeLog.md` (unmodified) |
| License | MIT |
| Build | Static library `glacier::miniz`, C99, with `MINIZ_NO_STDIO` and `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` |

Why miniz: MIT license, deflate built in (no dependency on Qt's private zlib),
archive I/O through callbacks (so saves can go through `QSaveFile`), streaming
single-entry reads, and raw entry copy between archives. `tests/tst_zip.cpp`
verifies these properties and corruption detection.

Why vendored rather than `FetchContent`: builds work offline and inside the
Flatpak builder, which has no network access during the build.

Known issue (3.1.2): when a write fails while an entry is being deflated
(for example on a full disk), `mz_zip_writer_add_mem_ex_v2` returns without
freeing its compressor state. `persistence::ZipWriter` therefore passes its
own allocator, which tracks live blocks and frees any left over when the
writer is destroyed; the vendored source stays unmodified. `tst_atomicsave`
under ASan covers this path. Recheck it when updating miniz.

To update: replace the files from the new release asset, update this table and
`THIRD_PARTY_NOTICES.md`, and run the full test suite including `asan`.

## Build and CI tools

CMake, Ninja, compilers, aqtinstall, and GitHub Actions are listed with their
pins in [TOOLCHAIN.md](TOOLCHAIN.md). None are shipped.
