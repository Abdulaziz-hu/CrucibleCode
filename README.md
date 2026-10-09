# Forge IDE

A lightweight, cross-platform, native C++ IDE inspired by Visual Studio Community.
Built with **C++20** and **Qt 6**. Windows is the primary target; Linux and macOS are supported.

Licensed under the **MIT License** (see `LICENSE`).

## Features (v0.1)

| Area | What you get |
|------|--------------|
| UI | Dense, flat, high-contrast chrome: 1px borders, square corners, flat buttons, VS-style blue status bar |
| Themes | **Dark Grey** (default) and **Light**, switchable at runtime (View > Theme / Tools > Settings), persisted |
| GPU | Window compositing through Qt's RHI (Direct3D 11/12 on Windows, OpenGL/Vulkan on Linux, Metal on macOS); `--no-gpu` and `--gpu-backend=` switches |
| Editor | Line-number gutter, current-line highlight, smart indent, Tab / Shift+Tab block indent, `Ctrl+/` comment toggle, Go to Line, zoom, CRLF + UTF-8 BOM preserving I/O, find / replace (case, whole word, regex) |
| Syntax | Built-in highlighter (C/C++, Python, CMake, JSON) and automatic use of **KSyntaxHighlighting** (MIT, 300+ languages) when installed |
| Project | Open any folder; Explorer tree with New File / New Folder / Rename / Delete / Reveal; drag and drop; recent folders |
| CMake | Configure, Build, Rebuild, Clean, Run; Debug / Release / RelWithDebInfo / MinSizeRel; custom generator and extra arguments; startup-item picker |
| Diagnostics | GCC, Clang, MSVC, CMake and linker output parsed into an **Error List**; double-click jumps to file:line:col |
| Search | Find in Files across the opened folder |

## Build

Requirements: Qt 6.4+ (Core, Gui, Widgets), CMake 3.21+, a C++20 compiler.
Optional: KSyntaxHighlighting (`KF6SyntaxHighlighting`).

Full, per-OS instructions (Windows, macOS, Ubuntu/Debian, Fedora, GPU backends, packaging):
**[docs/BUILDING.md](docs/BUILDING.md)**. Quick start:

```bash
# Ubuntu 24.04+ / Debian 12+
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev libxkbcommon-dev
# Fedora
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel mesa-libGL-devel libxkbcommon-devel
# macOS
brew install cmake ninja qt        # then add -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/forge                      # macOS: open build/forge.app
```

Windows (Qt MSVC kit + Visual Studio Build Tools):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2022_64
cmake --build build
```

## Command line

```
forge [paths...]            open a folder and/or files
forge --no-gpu              disable RHI window compositing
forge --gpu-backend=opengl  force a backend: opengl | vulkan | d3d11 | d3d12 | metal
```

## Keyboard shortcuts

| Shortcut | Action | Shortcut | Action |
|---|---|---|---|
| Ctrl+N / Ctrl+O | New / Open file | Ctrl+Shift+B | Build |
| Ctrl+Shift+O | Open folder | F5 | Build and run |
| Ctrl+S / Ctrl+Shift+S | Save / Save all | Shift+F5 | Stop |
| Ctrl+F / Ctrl+H | Find / Replace | Ctrl+Shift+F | Find in Files |
| Ctrl+G | Go to line | Ctrl+/ | Toggle comment |
| Ctrl+W | Close tab | Ctrl +/- / 0 | Zoom |

## Architecture

```
src/
  main.cpp            startup, GPU/RHI switches, command line
  MainWindow          docks, menus, toolbar, tabs, project tree, build wiring
  CodeEditor          QPlainTextEdit based editor + gutter + editing helpers
  SyntaxHighlighter   built-in fallback highlighter
  ThemeManager        Light / Dark Grey palettes + global flat stylesheet
  BuildManager        CMake CLI driver, output/diagnostic parser, runner
  FindBar             inline find / replace
  SearchPanel         Find in Files
  SettingsDialog      theme, editor font, CMake path / generator / args
```

Each unit has a narrow interface (signals/slots), so features can be added as new dock panels
without touching the editor core.

## GPU rendering - what it does and does not do

Qt Widgets draws with `QPainter` (CPU raster) into a backing store. Forge enables Qt's
**widgets-on-RHI** compositing (`QT_WIDGETS_RHI=1`), so the finished window content is composed
and presented by the GPU through Direct3D / OpenGL / Vulkan / Metal. That gives smooth scrolling,
resizing and docking, but glyph rasterization itself stays on the CPU. A fully GPU-rendered text
view would need a custom QRhi/Qt Quick editor surface; see the roadmap. If you see rendering
problems on an old driver, start with `--no-gpu`.

## Licensing notes

* Forge's own code: MIT.
* Qt 6 is used under **LGPL v3** via dynamic linking - keep it dynamically linked and ship Qt's license
  texts with binaries. (QScintilla was deliberately not used: it is GPL/commercial.)
* KSyntaxHighlighting (optional) is MIT.
* See `THIRD_PARTY.md`.

## Roadmap

* Debugger via the Debug Adapter Protocol (GDB / LLDB / MSVC)
* Language Server Protocol client (clangd): completion, diagnostics, go to definition
* CMake presets and File API (target list, per-target run/debug)
* Multi-cursor, code folding, bracket matching, minimap
* Git integration, integrated terminal
* Qt Quick / QRhi GPU text surface
* Plugin system
