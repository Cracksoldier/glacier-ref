# Toolchain pins

Reproducible CI pins are kept separate from the maintainer's rolling CachyOS
development environment (spec §6, milestone M0.02).

## CI pins

The Qt and aqtinstall versions live in one place,
[`.github/qt-version.env`](../.github/qt-version.env), which every workflow
reads.

| Item | Pin |
| --- | --- |
| Qt | 6.12.0 (latest stable when pinned on 2026-11-06) |
| Qt packages | `linux_gcc_64`, `win64_msvc2022_64`, `clang_64` (macOS), each with `qtimageformats` |
| aqtinstall | 3.3.0 (via install-qt-action; Linux and macOS only) |
| Windows Qt install | [`tools/ci/fetch_qt_windows.py`](../tools/ci/fetch_qt_windows.py): components `qtbase`, `qtsvg`, `qtimageformats` |
| Minimum Qt in `find_package` | 6.12 (equal to the pin; lower only with a CI job that builds against the lower version) |
| CMake | Minimum 3.21; CI uses the runner image's CMake (version recorded in each job log) |

| Job | Runner | Compiler | Architecture |
| --- | --- | --- | --- |
| Linux | `ubuntu-24.04` | GCC 14 (`gcc-14`/`g++-14`) | x86_64 |
| Windows | `windows-2025` | MSVC from the newest Visual Studio on the image (found with `vswhere`, version printed in the job log), Ninja Multi-Config | x64 |
| macOS | `macos-15` | Apple Clang (Xcode default of the image) | arm64 |

### GitHub Actions (pinned by commit SHA)

| Action | Version | Commit |
| --- | --- | --- |
| `actions/checkout` | v7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `jurplel/install-qt-action` | v4.5.0 | `b344308e4483fc892e0d27b3cd1b173ebdd95f06` |
| `actions/cache` | v6.1.0 | `55cc8345863c7cc4c66a329aec7e433d2d1c52a9` |
| `actions/upload-artifact` | v7.0.2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |
| `actions/download-artifact` | v8.0.2 | `9000827ccba6bdab643e8b6fd33ac0654aef8333` |

install-qt-action itself pins `actions/setup-python` by SHA (v6.3.0,
`ece7cb06caefa5fff74198d8649806c4678c61a1`). Dependabot proposes SHA updates
weekly as pull requests.

### Packaging tools

Pinned in the packaging scripts and verified by SHA-256 before use (see
[RELEASING.md](RELEASING.md)). `windeployqt` and `macdeployqt` come with the
Qt pin; the MSVC runtime comes from the Visual Studio used for the build.

| Tool | Release | SHA-256 |
| --- | --- | --- |
| linuxdeploy (`linuxdeploy-x86_64.AppImage`, bundles the AppImage plugin) | `1-alpha-20251107-1` | `c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d` |
| linuxdeploy-plugin-qt (`linuxdeploy-plugin-qt-x86_64.AppImage`) | `1-alpha-20250213-1` | `15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724` |

### Windows: why not aqtinstall

Current Qt releases (seen with 6.11.3 and 6.12.0) split the Windows online
repository into per-architecture folders, e.g.
`windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64/`, and their archives
unpack directly into the Qt prefix. aqtinstall 3.3.0, the latest release,
still looks for `qt6_6120/qt6_6120/Updates.xml` and fails. Linux and macOS
keep the old layout, so they continue to use install-qt-action.

`tools/ci/fetch_qt_windows.py` (standard library only) reads `Updates.xml`
from download.qt.io, picks only the requested components (excluding
debug-symbol packages), downloads them (mirrors allowed), verifies each
archive against the SHA-1 published on download.qt.io (metadata and
checksums must come from the official host), and extracts them with 7-Zip.
The result is cached per Qt version and script revision. Use `--dry-run`
to list the archive URLs. Switch Windows back to install-qt-action once an
aqtinstall release supports the new layout.

### Updating Qt

`.github/workflows/qt-update-check.yml` runs weekly. When aqtinstall reports a
newer stable Qt than the pin, it opens an issue; it never changes the pin. To
adopt a release: update `QT_VERSION` (and the `find_package` minimum and
`QT_DISABLE_DEPRECATED_UP_TO` if appropriate), update this file, and merge only
after CI passes on all platforms.

## Maintainer development environment (CachyOS)

Recorded 2026-11-06. This is a rolling distribution, so these versions change
over time and are not reproducibility pins.

| Item | Version |
| --- | --- |
| OS / kernel | CachyOS x86_64, Linux 7.3.1-1-cachyos |
| Desktop | KDE Plasma 6.8.2, KWin (Wayland) |
| Qt | 6.12.0 (`qt6-base` 6.12.0-3.1, `qt6-svg` 6.12.0-1.1, `qt6-imageformats` 6.12.0-1.1, `qt6-tools` 6.12.0-1.1) |
| GCC | 16.2.1 |
| Clang | 23.1.4 |
| CMake | 4.4.5 |
| Ninja | 1.13.2 |
| util-linux (`unshare`) | 2.42.4 |

Hardware is recorded in the specification (§9, reference machine).
