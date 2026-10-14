# Building CrucibleCode

## The easy way: setup scripts

`setup.sh` (Linux/macOS) and `setup.cmd` / `scripts\setup.ps1` (Windows) install the missing tools, build and start
CrucibleCode. See the README for options. What they install:

* **Windows:** Microsoft C++ Build Tools (winget, asks for UAC), Python 3 if needed; CMake, Ninja and `aqtinstall`
  go into a private virtual environment under `%LOCALAPPDATA%\CrucibleCode`, Qt 6.7.3 (MSVC 64-bit) into `%USERPROFILE%\Qt`.
  Qt DLLs are copied next to the exe with `windeployqt`.
* **Ubuntu / Debian, Fedora, Arch, openSUSE, Alpine:** compiler, CMake, Ninja, git, Qt 6 development packages through the
  system package manager (`sudo`). If the distribution's Qt is older than 6.4 (Ubuntu 22.04), Qt 6.7.3 is downloaded
  with `aqtinstall` into `~/Qt` instead.
* **macOS:** Xcode command line tools, Homebrew (if missing), then `cmake ninja git qt`.

## Bundled syntax highlighting (KSyntaxHighlighting)

The first CMake configure builds KSyntaxHighlighting (MIT) from source as a static library inside the build folder
(`build/_ksyntax`, log in `build/_ksyntax/build.log`). It needs `git` and network access once, takes a few minutes and is
cached afterwards. If an installed copy exists (`libkf6syntaxhighlighting-dev`, `kf6-syntax-highlighting-devel`, ...) it is
used instead. Options: `-DCRUCIBLE_BUNDLE_KSYNTAX=OFF` (never build it), `-DCRUCIBLE_USE_KSYNTAX=OFF` (built-in highlighter
only), `-DCRUCIBLE_KF_TAG=v6.9.0` (other KDE Frameworks release; 6.5+ needs Qt 6.5+). When anything fails the IDE falls back
to its built-in highlighter, which covers C, C++, C#, Markdown and many more languages.

## Python (live preview)

The **Go Live** button runs `resources/web/live_server.py` with your system Python 3.7+ (`python3`, `python` or `py -3`).
The setup scripts install Python; manually: Windows `winget install Python.Python.3.12`, macOS `brew install python`,
Ubuntu/Debian `sudo apt install python3`, Fedora `sudo dnf install python3`. No pip packages are needed.

## Optional Qt modules

Image formats WebP / TIFF / ICNS need Qt's `qtimageformats`, SVG needs `qtsvg`, the PDF viewer needs `qtpdf`.
Missing modules only disable that feature (PDF and unsupported images then open in your default application).

## Manual steps

Requirements everywhere: **Qt 6.4+** (Core, Gui, Widgets), **CMake 3.21+**, a **C++20** compiler.
Optional: KSyntaxHighlighting (`KF6SyntaxHighlighting`) for 300+ languages of highlighting.

Generic recipe (any OS):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release [-DCMAKE_PREFIX_PATH=<Qt kit>]
cmake --build build --parallel
```

`-DCRUCIBLE_USE_KSYNTAX=OFF` skips KSyntaxHighlighting even if it is installed.

---

## Ubuntu / Debian

Qt 6.4+ is required. That means **Ubuntu 24.04 or newer** or **Debian 12 or newer**.
Ubuntu 22.04 ships Qt 6.2, which is too old: use the Qt Online Installer (or `aqt`) instead and
pass `-DCMAKE_PREFIX_PATH=~/Qt/6.7.3/gcc_64`.

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git \
     qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev libxkbcommon-dev libvulkan-dev
# optional, richer syntax highlighting (package may not exist on older releases):
sudo apt install libkf6syntaxhighlighting-dev

git clone https://github.com/Abdulaziz-hu/CrucibleCode.git && cd CrucibleCode
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/CrucibleCode ~/path/to/a/cmake/project
```

Install system-wide (optional): `sudo cmake --install build --prefix /usr/local`

If Qt from the online installer fails to start with *"could not load the Qt platform plugin xcb"*:
`sudo apt install libxcb-cursor0`.

## Fedora

```bash
sudo dnf install gcc-c++ cmake ninja-build git \
     qt6-qtbase-devel mesa-libGL-devel libxkbcommon-devel vulkan-loader-devel
# optional:
sudo dnf install kf6-syntax-highlighting-devel
# optional, native Wayland support:
sudo dnf install qt6-qtwayland

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/CrucibleCode
```

Fedora's Qt 6 is found automatically; no `CMAKE_PREFIX_PATH` is needed. If CMake cannot find it,
use `-DCMAKE_PREFIX_PATH=/usr/lib64/cmake`.

## macOS (Intel and Apple Silicon)

```bash
xcode-select --install                 # Apple compiler toolchain
brew install cmake ninja qt

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
open build/CrucibleCode.app            # or: build/CrucibleCode.app/Contents/MacOS/CrucibleCode ~/my/project
```

Make a distributable app / disk image:

```bash
"$(brew --prefix qt)/bin/macdeployqt" build/CrucibleCode.app -dmg
```

Unsigned builds are blocked by Gatekeeper on other Macs: right-click > Open the first time, or run
`xattr -dr com.apple.quarantine CrucibleCode.app`. Public distribution needs an Apple Developer ID
(codesign + notarization). Rendering uses Metal automatically.

## Windows

Install Qt (MSVC 2022 64-bit kit) from the Qt Online Installer, plus Visual Studio 2022 Build Tools
(or Community) and CMake. From a *x64 Native Tools Command Prompt*:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2022_64
cmake --build build
cmake --install build --prefix dist      # copies Qt DLLs/plugins next to CrucibleCode.exe (Qt >= 6.5)
dist\bin\CrucibleCode.exe
```

Rendering uses Direct3D 11 by default.

---

## GPU backend selection

| OS | Default | Alternatives |
|----|---------|--------------|
| Windows | Direct3D 11 | `--gpu-backend=opengl`, `d3d12`, `vulkan` |
| Linux | OpenGL | `--gpu-backend=vulkan` |
| macOS | Metal | none |

`CrucibleCode --no-gpu` turns GPU compositing off (use it for broken drivers, VMs, remote desktops).

## Linux display server

CrucibleCode picks Wayland or X11 automatically. Force one with `QT_QPA_PLATFORM=xcb ./build/CrucibleCode` or
`QT_QPA_PLATFORM=wayland ./build/CrucibleCode`.

## Run-time tools

CrucibleCode only drives the `cmake` executable. It must be on `PATH` (or set under *Tools > Settings*),
and your compiler must be reachable from that shell: `g++`/`clang++` on Linux and macOS; on Windows
start CrucibleCode from a Developer Command Prompt (or set the generator to *Visual Studio 17 2022*).

## Run-time tools for your own projects

* **C / C++:** `cmake`, a compiler and (optionally) Ninja on `PATH`.
* **C#:** the .NET SDK (`dotnet`): Ubuntu `sudo apt install dotnet-sdk-8.0`, Fedora `sudo dnf install dotnet-sdk-8.0`,
  macOS `brew install --cask dotnet-sdk`, Windows `winget install Microsoft.DotNet.SDK.8`.

## Integrated terminal requirements

* Linux / macOS: nothing extra (uses `forkpty`; links `libutil` on older glibc automatically).
* Windows: **Windows 10 version 1809 or newer** (ConPTY). PowerShell 7, Windows PowerShell, Command Prompt,
  Git Bash and WSL are detected automatically.

## Building packages

| Script | Host | Produces |
|--------|------|----------|
| `./build-linux.sh` | Linux | `cruciblecode_<ver>_<arch>.deb`, `cruciblecode-<ver>-1.<arch>.rpm`, `CrucibleCode-<ver>-<arch>.AppImage` |
| `.\build-windows.ps1` (or `build-windows.cmd`) | Windows | `CrucibleCode-<ver>-windows-x64-portable.zip`, `...-setup.exe`, `....msi` |

All output goes to `dist/`, with a `<file>.sha256` next to each package. The scripts refuse to run on the wrong operating system.

### Linux (`build-linux.sh`)
* Options: `--formats deb,rpm,appimage`, `--version X`, `--no-ksyntax`, `--qt-prefix DIR`, `--skip-build`, `--clean`, `--jobs N`, `--build-dir DIR`.
* `.deb` needs `dpkg-deb` (dependencies are computed with `dpkg-shlibdeps` when available), `.rpm` needs `rpmbuild`
  (`sudo apt install rpm` / `sudo dnf install rpm-build`), the AppImage needs `curl` or `wget` and Qt's `qmake6`
  (linuxdeploy + its Qt plugin are downloaded into `build/tools`; `APPIMAGE_EXTRACT_AND_RUN=1` is set so FUSE is not required).
* Packages install to `/usr` (`/usr/bin/CrucibleCode`, desktop file, icons).

### Windows (`build-windows.ps1`)
* Parameters: `-Formats portable,installer,msi`, `-Version`, `-QtDir C:\Qt\6.7.3\msvc2022_64`, `-NoKSyntax`, `-SkipBuild`, `-Clean`, `-Generator`.
* Finds Qt (`-QtDir`, `Qt6_DIR`, `QTDIR`, `C:\Qt`) and loads the Visual Studio x64 environment (or MinGW for MinGW kits) by itself.
* Qt is deployed with `windeployqt`. Each package contains an `install-type.txt` (portable / installer / msi) that the updater reads.
* **Installer**: Inno Setup 6 (`winget install JRSoftware.InnoSetup`), script in `packaging/windows/cruciblecode.iss`.
  Per-user by default, the wizard can install for all users.
* **MSI**: WiX Toolset v4+ (`dotnet tool install --global wix`; the script installs it and the UI extension if the .NET SDK is present). Per-machine install with a Start-menu shortcut and upgrade handling.
* The portable ZIP needs no extra tools.
