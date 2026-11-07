#!/usr/bin/env bash
# Builds the Glacier Ref AppImage from a configured and built CMake tree.
#
#   packaging/linux/build-appimage.sh <build-dir> <version> <output-dir>
#
# Needs QT_ROOT_DIR (the Qt prefix the tree was built against), curl and
# sha256sum. Baseline: Ubuntu 24.04 (glibc 2.39), see docs/RELEASING.md.
# linuxdeploy and its Qt plugin are pinned to exact releases and verified by
# SHA-256 (spec §6).
set -euo pipefail

LINUXDEPLOY_TAG=1-alpha-20251107-1
LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
PLUGIN_QT_TAG=1-alpha-20250213-1
PLUGIN_QT_SHA256=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724

die() { echo "build-appimage: $*" >&2; exit 1; }

[ $# -eq 3 ] || die "usage: $0 <build-dir> <version> <output-dir>"
[ -n "${QT_ROOT_DIR:-}" ] || die "QT_ROOT_DIR is not set"
build=$(realpath "$1")
version=$2
mkdir -p "$3"
out=$(realpath "$3")

work=$(mktemp -d)
tools="$work/tools"
appdir="$work/AppDir"
mkdir -p "$tools"

fetch() { # url sha256 file
    curl -fsSL --retry 3 -o "$tools/$3" "$1"
    echo "$2  $tools/$3" | sha256sum -c --quiet - || die "checksum mismatch for $3"
    chmod +x "$tools/$3"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage" \
    "$LINUXDEPLOY_SHA256" linuxdeploy-x86_64.AppImage
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$PLUGIN_QT_TAG/linuxdeploy-plugin-qt-x86_64.AppImage" \
    "$PLUGIN_QT_SHA256" linuxdeploy-plugin-qt-x86_64.AppImage
export PATH="$tools:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1 # no FUSE on CI runners
linuxdeploy=("$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir")

DESTDIR="$appdir" cmake --install "$build" --prefix /usr

# Qt: libraries, xcb and the native Wayland platform plugins, image formats.
export QMAKE="$QT_ROOT_DIR/bin/qmake"
mapfile -t wayland < <(cd "$QT_ROOT_DIR/plugins/platforms" && ls libqwayland*.so 2>/dev/null)
[ ${#wayland[@]} -gt 0 ] || die "no Wayland platform plugin in $QT_ROOT_DIR/plugins/platforms"
EXTRA_PLATFORM_PLUGINS=$(IFS=';'; echo "${wayland[*]}")
export EXTRA_PLATFORM_PLUGINS
"${linuxdeploy[@]}" --plugin qt

# The Wayland client also needs its shell, decoration and graphics plugins.
for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    if [ ! -d "$appdir/usr/plugins/$dir" ] && [ -d "$QT_ROOT_DIR/plugins/$dir" ]; then
        echo "build-appimage: adding $dir"
        cp -r "$QT_ROOT_DIR/plugins/$dir" "$appdir/usr/plugins/"
        "${linuxdeploy[@]}" --deploy-deps-only "$appdir/usr/plugins/$dir"
    fi
done

required=(
    usr/bin/glacier-ref
    usr/plugins/platforms/libqxcb.so
    usr/plugins/wayland-shell-integration/libxdg-shell.so
    usr/plugins/imageformats/libqjpeg.so
    usr/plugins/imageformats/libqgif.so
    usr/plugins/imageformats/libqwebp.so
    usr/plugins/imageformats/libqtiff.so
    usr/lib/libQt6Svg.so.6
    usr/lib/libQt6WaylandClient.so.6
)
for plugin in "${wayland[@]}"; do required+=("usr/plugins/platforms/$plugin"); done
for file in "${required[@]}"; do
    [ -e "$appdir/$file" ] || die "missing from the AppDir: $file"
done

name="glacier-ref-$version-linux-x86_64.AppImage"
(cd "$work" && LDAI_OUTPUT="$name" OUTPUT="$name" "${linuxdeploy[@]}" --output appimage)
mv "$work/$name" "$out/$name"
rm -rf "$work"
echo "build-appimage: wrote $out/$name"
