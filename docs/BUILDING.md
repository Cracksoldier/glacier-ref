# Building Glacier Ref

Requirements on every platform:

- CMake 3.21 or newer, Ninja (Linux/macOS)
- A C++20 compiler: GCC 13+, Clang 16+, MSVC 2022, or Apple Clang 15+
- Qt **6.11 or newer** with the modules Core, Gui, Widgets, Svg, Concurrent,
  Test, plus the **Qt Image Formats** plugins (WebP, TIFF)

The minimum Qt is 6.11, the Qt of the Flatpak's KDE runtime, which CI also
builds and tests against; all other CI jobs use the 6.12.0 pin (see
[TOOLCHAIN.md](TOOLCHAIN.md)). Configure fails with an older Qt. Where a distribution ships an older Qt,
install the pinned Qt with [aqtinstall](https://github.com/miurahr/aqtinstall)
and pass `-DCMAKE_PREFIX_PATH=<qt>/6.12.0/<arch>`.

## Presets

| Configure preset | Purpose |
| --- | --- |
| `dev-debug` / `dev-release` | Local Ninja builds |
| `asan` | Debug with AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) |
| `ci-release` / `ci-asan` | As above with warnings as errors |
| `windows-msvc` / `ci-windows` | MSVC x64 with Ninja Multi-Config; run from an x64 Developer PowerShell |

Build and test presets have the same names (Windows: `windows-debug`,
`windows-release`, `ci-windows-debug`, `ci-windows-release`). Build trees go
to `build/<preset>/`.

## CachyOS / Arch Linux (primary)

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-svg qt6-imageformats
cmake --preset dev-release
cmake --build --preset dev-release
ctest --preset dev-release
./build/dev-release/app/glacier-ref
```

The Wayland platform plugin ships in `qt6-base`. Tests also run under the
`de_AT.UTF-8` locale; enable it in `/etc/locale.gen` and run `sudo locale-gen`
if it is missing.

`./build/dev-release/app/glacier-ref --platform-info` prints the active Qt
platform plugin (it should be `wayland` in a Plasma Wayland session), session
type, desktop, and screens.

Optional: `sudo cmake --install build/dev-release --prefix /usr/local` installs
the binary, desktop entry, and icon.

## Developer tools (datasets and benchmark)

```sh
build/dev-release/tools/fixturegen/glacier-fixturegen --output /data/glacier-fixtures   # standard + stress
QT_QPA_PLATFORM=offscreen tools/perf/run-constrained.sh build/dev-release/tools/bench/glacier-bench \
    --dataset /data/glacier-fixtures/standard --size 2560x1440 --json results.json
```

The datasets are generated deterministically (`--seed`) and never committed. Put them on a filesystem with ample free space, not a RAM-backed `/tmp`. `--screen HDMI-A-1` runs the benchmark full screen on a real output instead of offscreen. Tools build by default; disable them with `-DGLACIER_BUILD_TOOLS=OFF`.

## Fedora

```sh
sudo dnf install cmake ninja-build gcc-c++ glibc-langpack-de \
    qt6-qtbase-devel qt6-qtsvg-devel qt6-qtimageformats
```

Then use the same preset commands. If Fedora's Qt is older than 6.11, use the
pinned Qt via aqtinstall as described above.

## Windows 11 (x64)

1. Install Visual Studio 2022 with the *Desktop development with C++* workload
   (includes CMake).
2. Install Qt 6.12.0 for MSVC 2022 64-bit with *Qt Image Formats*, using the
   Qt Online Installer or aqtinstall:
   `aqt install-qt windows desktop 6.12.0 win64_msvc2022_64 -m qtimageformats -O C:\Qt`
3. From an x64 *Developer PowerShell* (or *x64 Native Tools Command Prompt*)
   for your Visual Studio version (2022 or newer):

```powershell
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64
cmake --build --preset windows-release
$env:PATH = "C:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"
ctest --preset windows-release
.\build\windows-msvc\app\Release\glacier-ref.exe
```

Running outside a Qt-enabled shell requires `windeployqt`; packaging is
covered in milestone M5.

## macOS (Apple Silicon, secondary)

```sh
xcode-select --install
brew install cmake ninja
aqt install-qt mac desktop 6.12.0 clang_64 -m qtimageformats -O ~/Qt
cmake --preset dev-release -DCMAKE_PREFIX_PATH=~/Qt/6.12.0/macos -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build --preset dev-release
ctest --preset dev-release
open "build/dev-release/app/glacier-ref.app"
```
