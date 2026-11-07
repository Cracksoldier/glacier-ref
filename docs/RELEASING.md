# Releasing Glacier Ref

The [Release workflow](../.github/workflows/release.yml) builds, tests and
packages Glacier Ref on all three platforms (M5.01). Packaging scripts live in
[`packaging/`](../packaging) and also run outside CI.

| Package | Script | Runner | Contents |
| --- | --- | --- | --- |
| `glacier-ref-<version>-windows-x64.zip` | `packaging/windows/stage.ps1` | `windows-2025`, MSVC, Qt pin | `cmake --install`, `windeployqt` (no translations, no Mesa or D3D compiler), the MSVC runtime copied app-locally, `LICENSE.txt`, `THIRD_PARTY_NOTICES.md` |
| `glacier-ref-<version>-linux-x86_64.AppImage` | `packaging/linux/build-appimage.sh` | `ubuntu-24.04`, GCC 14, Qt pin | linuxdeploy with its Qt plugin; xcb plus the native Wayland platform plugins and their shell, decoration and graphics plugins; desktop entry and icon |
| `glacier-ref-<version>-macos-arm64.dmg` | `packaging/macos/build-dmg.sh` | `macos-15`, Apple Clang, Qt pin | `Glacier Ref.app` staged with `macdeployqt`, ad-hoc signed, an Applications link, `LICENSE.txt` |

Each job first runs the full test suite (both locales) on the Release build,
then packages, then smoke-tests the package **with the CI Qt directory moved
away and an empty environment**: `--version`, `--check-image-formats` (every v1
format, WebP and TIFF included, decoded through the shipped plugins) and
`--platform-info` (the platform plugin loads; xvfb on Linux). The packaging
scripts also fail if a required plugin or library is missing from the package.

**Linux baseline:** Ubuntu 24.04, so the AppImage needs glibc 2.39 or newer
(Fedora 40+, Ubuntu 24.04+, current Arch/CachyOS). The Qt pin itself is built
on RHEL 9.6. **macOS:** Apple Silicon only (minimum macOS as required by the Qt pin), ad-hoc signed, not notarized.

## Tester builds

1. Make sure `main` is green in CI.
2. Tag the commit and push the tag. The version must start with the
   `project(VERSION …)` in `CMakeLists.txt`; add a suffix for test builds:

   ```sh
   git tag -a v0.1.0-test.1 -m "Glacier Ref 0.1.0-test.1"
   git push origin v0.1.0-test.1
   ```

3. The workflow creates a **draft** release with the three packages,
   `SHA256SUMS.txt` and the notes from
   [`packaging/release-notes.md`](../packaging/release-notes.md). Versions
   below 1.0 and versions with a suffix are marked as pre-releases.
4. Check the draft on GitHub (Releases), then **Publish release**. Testers
   download from the release page without a GitHub account.

Re-running the workflow for the same tag replaces the assets of the existing
release. A manual run (**Actions ▸ Release ▸ Run workflow**) builds
`<version>-dev.<commit>` packages as run artifacts only (14 days, GitHub login
needed to download) and creates no release.

## Pins

Packaging tools are pinned to exact releases and verified by SHA-256 in the
scripts; GitHub Actions are pinned by commit (see [TOOLCHAIN.md](TOOLCHAIN.md)).

## Not yet covered (M5)

- Windows installer (Inno Setup, M5.03) and the portable marker for the ZIP
  (M5.04): the ZIP currently keeps its data in the user profile.
- Flatpak bundle (M5.06).
- Clean-machine validation on Windows 11, Fedora and macOS (M5.02, M5.05,
  M5.07, M5.10), signing and notarization (M5.08), the complete notices and
  source offer for the bundled Qt (M5.09).
