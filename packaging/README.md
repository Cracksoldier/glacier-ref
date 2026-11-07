# packaging

Release packaging (milestone M5). All packaging tools are pinned to exact
versions (spec §6). How to cut a tester or release build: [docs/RELEASING.md](../docs/RELEASING.md).

| Path | Produces |
| --- | --- |
| `windows/stage.ps1` | Staged folder and `glacier-ref-<version>-windows-x64.zip` |
| `linux/build-appimage.sh` | `glacier-ref-<version>-linux-x86_64.AppImage` |
| `macos/build-dmg.sh` | Staged `Glacier Ref.app` and `glacier-ref-<version>-macos-arm64.dmg` |
| `release-notes.md` | Notes template for the draft GitHub release |

Still to come: Inno Setup installer and portable marker (Windows), Flatpak
manifest (Linux).
