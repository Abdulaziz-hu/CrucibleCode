# Publishing a release (and how the updater finds it)

CrucibleCode's updater talks to the GitHub Releases API of the repository in `src/AppInfo.h`
(`https://api.github.com/repos/<owner>/<repo>/releases/latest`).

1. Bump the version in `CMakeLists.txt` (`project(... VERSION x.y.z)`), update `CHANGELOG.md`.
2. Build the packages: `./build-linux.sh` on Linux, `.\build-windows.ps1` on Windows.
3. Create a GitHub release whose **tag is `v<version>`** (for example `v0.7.1`). The release description is shown
   as Markdown in the update popup, so write the release notes there.
4. Upload everything from `dist/` as release assets, including the `.sha256` files.

## How an installed copy picks its asset

| Installation | Detected by | Asset used (file name match) |
|--------------|-------------|------------------------------|
| AppImage | `$APPIMAGE` is set | `*.AppImage` |
| .deb | binary under `/usr` or `/opt` and `dpkg -S` knows it | `*.deb` |
| .rpm | binary under `/usr` or `/opt` and `rpm -qf` knows it | `*.rpm` |
| Portable ZIP (Windows) | `install-type.txt` = `portable` | `*.zip` containing `win` or `portable` |
| Installer (Windows) | `install-type.txt` = `installer` | `*.exe` containing `setup` or `install` |
| MSI (Windows) | `install-type.txt` = `msi` | `*.msi` |

Builds made from source are not updatable; they open the release page.
The asset is downloaded to the cache folder, its size and (if `<asset>.sha256` exists) SHA-256 are verified, then a helper
script waits for CrucibleCode to exit, replaces / installs the package and starts the new version
(AppImage: `cp` + `mv`; deb / rpm: `pkexec apt-get install` / `dnf` / `rpm -U`; Windows: PowerShell helper that expands the ZIP, runs the
installer with `/VERYSILENT` or `msiexec /passive`). Pre-releases are only offered when enabled in Settings > Updates.
