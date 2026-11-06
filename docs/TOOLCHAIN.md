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
| aqtinstall | 3.3.0 (via install-qt-action) |
| Minimum Qt in `find_package` | 6.12 (equal to the pin; lower only with a CI job that builds against the lower version) |
| CMake | Minimum 3.21; CI uses the runner image's CMake (version recorded in each job log) |

| Job | Runner | Compiler | Architecture |
| --- | --- | --- | --- |
| Linux | `ubuntu-24.04` | GCC 14 (`gcc-14`/`g++-14`) | x86_64 |
| Windows | `windows-2025` | MSVC, Visual Studio 2022 generator | x64 |
| macOS | `macos-15` | Apple Clang (Xcode default of the image) | arm64 |

### GitHub Actions (pinned by commit SHA)

| Action | Version | Commit |
| --- | --- | --- |
| `actions/checkout` | v7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| `jurplel/install-qt-action` | v4.5.0 | `b344308e4483fc892e0d27b3cd1b173ebdd95f06` |

install-qt-action itself pins `actions/setup-python` by SHA (v6.3.0,
`ece7cb06caefa5fff74198d8649806c4678c61a1`). Dependabot proposes SHA updates
weekly as pull requests.

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
