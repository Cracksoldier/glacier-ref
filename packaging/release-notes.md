**Test build — not a finished release.** Built from commit `@COMMIT@`. Packages are unsigned and experimental (spec §7); please report what you find.

| System | Download | How to start |
| --- | --- | --- |
| Windows 11 x64 | `glacier-ref-@VERSION@-windows-x64.zip` | Extract the whole folder, run `glacier-ref.exe`. SmartScreen warns about an unknown publisher: **More info ▸ Run anyway**. |
| Linux x86_64 (glibc 2.39+: Fedora 40+, Ubuntu 24.04+, Arch/CachyOS) | `glacier-ref-@VERSION@-linux-x86_64.AppImage` | `chmod +x` the file, then run it. Runs natively on Wayland and on X11. If it does not start, try `--appimage-extract-and-run`. |
| Linux x86_64, Flatpak | `glacier-ref-@VERSION@-linux-x86_64.flatpak` | `flatpak install --user glacier-ref-@VERSION@-linux-x86_64.flatpak`, then start *Glacier Ref* from the menu or with `flatpak run io.github.cracksoldier.GlacierRef`. The KDE runtime comes from Flathub on first install. The Flatpak sees your home folder only; it has no network access. |
| macOS on Apple Silicon | `glacier-ref-@VERSION@-macos-arm64.dmg` | Drag *Glacier Ref* to Applications. It is not notarized: on first start open **System Settings ▸ Privacy & Security ▸ Open Anyway**, or run `xattr -dr com.apple.quarantine "/Applications/Glacier Ref.app"`. |

Check a download against `SHA256SUMS.txt` (`sha256sum -c SHA256SUMS.txt --ignore-missing`; Windows: `Get-FileHash <file>`).

Good to know:
- Glacier Ref works fully offline. The only link that opens a browser is **Help ▸ Project Website**.
- The menu bar is hidden: press **Tab**, click **≡** (top left) or right-click the canvas. **F1** opens the help.
- The Windows ZIP is not a portable version yet: settings and recovery data go to your user profile, like an installed program.
- Settings, recovery and log folders are listed in **Edit ▸ Preferences ▸ General**. For a bug report, attach **Help ▸ Copy Diagnostics** (home paths and your user name are removed) and the newest log file.
