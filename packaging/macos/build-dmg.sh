#!/usr/bin/env bash
# Stages Glacier Ref.app with macdeployqt and writes the DMG (Apple Silicon).
#
#   packaging/macos/build-dmg.sh <build-dir> <version> <output-dir>
#
# Needs QT_ROOT_DIR (the Qt prefix the tree was built against). Leaves the
# staged bundle in <output-dir>/stage/Glacier Ref.app for the smoke test.
# The bundle is only ad-hoc signed (Apple Silicon requires a signature) and
# not notarized: no release credentials yet (spec §7, M5.08).
set -euo pipefail

die() { echo "build-dmg: $*" >&2; exit 1; }

[ $# -eq 3 ] || die "usage: $0 <build-dir> <version> <output-dir>"
[ -n "${QT_ROOT_DIR:-}" ] || die "QT_ROOT_DIR is not set"
build=$(cd "$1" && pwd)
version=$2
mkdir -p "$3"
out=$(cd "$3" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)

stage="$out/stage"
app="$stage/Glacier Ref.app"
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$build/app/glacier-ref.app" "$app"

"$QT_ROOT_DIR/bin/macdeployqt" "$app" -always-overwrite
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

required=(
    Contents/MacOS/glacier-ref
    Contents/PlugIns/platforms/libqcocoa.dylib
    Contents/PlugIns/imageformats/libqjpeg.dylib
    Contents/PlugIns/imageformats/libqgif.dylib
    Contents/PlugIns/imageformats/libqwebp.dylib
    Contents/PlugIns/imageformats/libqtiff.dylib
    Contents/Frameworks/QtSvg.framework
    Contents/Resources/qt.conf
)
for file in "${required[@]}"; do
    [ -e "$app/$file" ] || die "missing from the bundle: $file"
done

dmgroot=$(mktemp -d)
cp -R "$app" "$dmgroot/"
ln -s /Applications "$dmgroot/Applications"
cp "$root/LICENSE" "$dmgroot/LICENSE.txt"
name="glacier-ref-$version-macos-arm64.dmg"
rm -f "$out/$name"
# hdiutil occasionally reports "Resource busy" on CI runners; retry.
for attempt in 1 2 3; do
    if hdiutil create -volname "Glacier Ref" -srcfolder "$dmgroot" -fs HFS+ -format UDZO -ov "$out/$name"; then
        break
    fi
    [ "$attempt" -lt 3 ] || die "hdiutil failed"
    sleep 5
done
rm -rf "$dmgroot"
echo "build-dmg: wrote $out/$name"
