# Building Forge IDE

Requirements everywhere: **Qt 6.4+** (Core, Gui, Widgets), **CMake 3.21+**, a **C++20** compiler.
Optional: KSyntaxHighlighting (`KF6SyntaxHighlighting`) for 300+ languages of highlighting.

Generic recipe (any OS):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release [-DCMAKE_PREFIX_PATH=<Qt kit>]
cmake --build build --parallel
```

`-DFORGE_USE_KSYNTAX=OFF` skips KSyntaxHighlighting even if it is installed.

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

git clone <your repo> forge-ide && cd forge-ide    # or unzip forge-ide.zip
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/forge ~/path/to/a/cmake/project
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
./build/forge
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
open build/forge.app                   # or: build/forge.app/Contents/MacOS/forge ~/my/project
```

Make a distributable app / disk image:

```bash
"$(brew --prefix qt)/bin/macdeployqt" build/forge.app -dmg
```

Unsigned builds are blocked by Gatekeeper on other Macs: right-click > Open the first time, or run
`xattr -dr com.apple.quarantine forge.app`. Public distribution needs an Apple Developer ID
(codesign + notarization). Rendering uses Metal automatically.

## Windows

Install Qt (MSVC 2022 64-bit kit) from the Qt Online Installer, plus Visual Studio 2022 Build Tools
(or Community) and CMake. From a *x64 Native Tools Command Prompt*:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2022_64
cmake --build build
cmake --install build --prefix dist      # copies Qt DLLs/plugins next to forge.exe (Qt >= 6.5)
dist\bin\forge.exe
```

Rendering uses Direct3D 11 by default.

---

## GPU backend selection

| OS | Default | Alternatives |
|----|---------|--------------|
| Windows | Direct3D 11 | `--gpu-backend=opengl`, `d3d12`, `vulkan` |
| Linux | OpenGL | `--gpu-backend=vulkan` |
| macOS | Metal | none |

`forge --no-gpu` turns GPU compositing off (use it for broken drivers, VMs, remote desktops).

## Linux display server

Forge picks Wayland or X11 automatically. Force one with `QT_QPA_PLATFORM=xcb ./build/forge` or
`QT_QPA_PLATFORM=wayland ./build/forge`.

## Using Forge with its own build tools

Forge only drives the `cmake` executable. It must be on `PATH` (or set under *Tools > Settings*),
and your compiler must be reachable from that shell: `g++`/`clang++` on Linux and macOS; on Windows
start Forge from a Developer Command Prompt (or set the generator to *Visual Studio 17 2022*).
