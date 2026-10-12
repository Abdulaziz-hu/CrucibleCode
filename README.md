<p align="center"><img src="resources/icons/icon-128.png" alt="CrucibleCode logo" width="96"></p>

# CrucibleCode

> Repository: **https://github.com/Abdulaziz-hu/CrucibleCode** - issues and pull requests are welcome.

A lightweight, cross-platform, native IDE for **C, C++ and C#**, inspired by Visual Studio Community.
Built with **C++20** and **Qt 6**. Windows is the primary target; Linux and macOS are fully supported.

Licensed under the **MIT License** (see `LICENSE`).

## Features (v0.6)

| Area | What you get |
|------|--------------|
| UI | Dense, flat, high-contrast chrome: 1px borders, square corners, flat buttons, VS-style blue status bar |
| Themes | **Dark Grey** (default) and **Light**, switchable at runtime (View > Theme / Tools > Settings), persisted |
| Panels | Every panel can be dragged, tabbed, floated and **resized**; **double-click a splitter** to reset that panel to its default size (View > Reset Panel Sizes resets all) |
| Terminal | **Built-in native terminals** (real pseudo terminals: ConPTY on Windows, `forkpty` on Linux/macOS). PowerShell, cmd, Git Bash, WSL, bash, zsh, fish... **Open as many as you like** (`+` button, shell picker `v`, Ctrl+Shift+Backtick), close any of them (tab `x`, `Kill Terminal`, or just `exit`). 256-colour/truecolor, scrollback, selection, copy/paste, Ctrl+Backtick toggles the panel |
| Projects | Open a folder: Explorer tree with New File / Folder / Rename / Delete. **Tabs are per project**: opening another project closes the previous project's tabs (with unsaved-changes prompts) and re-opens the tabs that project had last time |
| C / C++ | Highlighting, CMake configure / build / rebuild / clean / run, GCC, Clang, MSVC and linker diagnostics in an Error List |
| C# | Highlighting, `.sln` / `.csproj` detection, build / clean / run through the `dotnet` CLI, MSBuild diagnostics |
| Markdown | Syntax highlighting (headings, emphasis, code, fenced blocks, links, lists, quotes) and a **live preview** dock (`Ctrl+Shift+V`) |
| Editor | Line numbers, current-line highlight, **bracket matching**, smart indent, block indent, `Ctrl+/` comment, word wrap, Go to Line, find / replace (case, whole word, regex), Find in Files, zoom, CRLF and BOM preserving I/O |
| Syntax | Built-in highlighter (C, C++, C#, Markdown, CMake, Python, JSON) and automatic use of **KSyntaxHighlighting** (MIT, 300+ languages) when installed. Help > About lists everything |
| Source control | A **Source Control** tab next to the Explorer, like VS Code: branch switcher, staged / unstaged changes, stage / unstage / discard, commit box (`Ctrl+Enter`), fetch / pull / push (or *Publish*), recent commits, diff tabs, and **coloured gutter bars** for added / modified / deleted lines. Uses the `git` command line (`Ctrl+Shift+G`) |
| Find / Replace | `Ctrl+F` opens the find bar; the arrow on its left (or `Ctrl+H`) expands **Replace**. Both fields have a history dropdown, **every match is highlighted while you type**, with an "3 of 12" counter, case / whole-word / regex options |
| Web development | HTML / CSS / JS (and JSON, SVG) with highlighting, tag and property completion and **auto-closing tags**. While you edit a web file a **Go Live** button appears in the top-right of the tab bar: it starts a local **Python** server for your folder, opens the page in your browser and **reloads it every time you save** (CSS-only changes are applied without a reload). Click again to turn it off; *Open in Browser*, the **Web** menu (`Ctrl+Alt+L` / `Ctrl+Alt+B`) and the port are available too. Needs Python 3.7+, standard library only |
| Cursor | Solid while you type, blinking only when idle (Tools > Settings: always / never blink); same in the terminal |
| Viewers | Open **images** (PNG, JPEG, GIF, BMP, ICO, ICNS, WebP, TIFF, SVG: zoom, fit, pan, multi-size icons), **PDF** documents (Qt PDF) and see an info page with *Open with default application* for Office files, archives, media and other binaries |
| Run a file | Right-click any script or program in the Explorer (or a tab) and choose **Run**: `.sh`, `.ps1`, `.bat`, `.py`, `.js`, `.rb`, `.pl`, `.php`, `.lua`, `.jar`, `.go`, executables, files with a shebang. It runs in an integrated terminal. `Ctrl+F5` runs the current file |
| Markdown | A **Preview** button appears in the top-right of the tab bar while a Markdown file is open |
| Spelling | Markdown and text files get a **red underline** under misspelled words; right-click for corrections, *Add to dictionary* or *Ignore*. Built-in English dictionary, works offline; code blocks, inline code and URLs are skipped |
| Suggestions | Off by default (Tools > Settings): a completion popup with the language's keywords and words from the file (`Ctrl+Space`) |
| AI completion | Off by default (Tools > Settings): grey inline suggestions, `Tab` to accept, from a **small local model** (default Qwen2.5-Coder 0.5B, or any GGUF you choose). Nothing leaves your computer; the one-time downloads (llama.cpp runtime + model) start only when you ask |
| Explorer | **Hidden files are shown by default** (`.gitignore`, `.env`, ...); turn that off in Tools > Settings. New / rename / delete, *Open in Terminal*, *Copy Path* |
| Navigation | **Go to File** fuzzy finder (`Ctrl+P`), Find in Files (also searches dot-files), Go to Line, bracket matching, tab menu (close others, copy path, open containing folder) |
| Safety | Files changed outside the IDE are detected and reloaded (asks first if you have unsaved edits); unsaved-changes prompts everywhere |
| Scrolling / zoom | `Shift` + mouse wheel scrolls sideways; `Ctrl` + wheel (or `Ctrl` +/-) zooms, and the zoom now survives theme switches, settings changes and word wrap |
| Editing | Move line `Alt+Up/Down`, duplicate `Shift+Alt+Up/Down`, delete line `Ctrl+Shift+K`, configurable tab size |
| Startup item | The toolbar combo lists every executable target: read from CMake's File API once configured, from `add_executable()` before that, and `.csproj` files for C# |
| GPU | Window compositing through Qt's RHI (Direct3D 11/12, OpenGL, Vulkan, Metal); `--no-gpu` and `--gpu-backend=` switches |

## Quick start: one script does everything

The setup scripts **download and install what is missing** (compiler, CMake, Ninja, Qt, optional .NET SDK),
**build** CrucibleCode and **start** it. They only ask for administrator rights when something system-wide
has to be installed, and they do nothing for tools that are already present.

| OS | Command |
|----|---------|
| Windows | `setup.cmd` (or `powershell -ExecutionPolicy Bypass -File scripts\setup.ps1`). UAC asks for permission only if the C++ Build Tools / Python must be installed |
| Linux (Ubuntu, Debian, Fedora, RHEL, Arch, openSUSE, Alpine) | `./setup.sh` (uses `sudo` just for packages; run it as your normal user) |
| macOS | `./setup.sh` (installs Xcode tools + Homebrew + Qt if needed; do not use `sudo`) |

Useful options: `--debug` / `-Config Debug`, `--no-run` / `-NoRun`, `--clean` / `-Clean`, `--with-dotnet` / `-WithDotNet`
(C# support), `--use-aqt` (Linux/macOS: take Qt from the official Qt servers), `--repo <url>` / `-Repo <url>` (clone your fork first).

Straight from GitHub (no clone needed, the script fetches the sources itself):

```bash
# Linux / macOS
curl -fsSL https://raw.githubusercontent.com/Abdulaziz-hu/CrucibleCode/main/scripts/setup.sh -o setup.sh
bash setup.sh
```
```powershell
# Windows
irm https://raw.githubusercontent.com/Abdulaziz-hu/CrucibleCode/main/scripts/setup.ps1 -OutFile setup.ps1
powershell -ExecutionPolicy Bypass -File setup.ps1
```

Built a fork? Add `--repo https://github.com/<you>/CrucibleCode.git` (Windows: `-Repo ...`).

## Manual build

Requirements: Qt 6.4+ (Core, Gui, Widgets), CMake 3.21+, a C++20 compiler. Optional: KSyntaxHighlighting.
Run-time requirements: `cmake` for C/C++ projects, the `dotnet` SDK for C# projects.
Full per-OS instructions: **[docs/BUILDING.md](docs/BUILDING.md)**.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release     # add -DCMAKE_PREFIX_PATH=<Qt> if needed
cmake --build build --parallel
./build/CrucibleCode               # macOS: open build/CrucibleCode.app
```

## About this project

CrucibleCode is a hobby project made **for fun and learning**. AI tools were used in certain areas of the software
(code and documentation). It is provided as is, without any warranty (see `LICENSE`).

## Logo

A crucible with a rising ember: flat, square-cornered, two colours plus white. Files in `resources/`:
`logo/icon.svg` (blue tile), `logo/icon-mono.svg` (single colour, `currentColor`), `logo/logo-horizontal.svg`
(icon + wordmark), `icons/*.png`, `icons/icon.ico`, `icons/icon.icns`. Regenerate the raster files with
`python tools/make_icons.py`.

## Command line

```
CrucibleCode [paths...]            open folders and/or files (folders first)
CrucibleCode --no-gpu              disable RHI window compositing
CrucibleCode --gpu-backend=opengl  opengl | vulkan | d3d11 | d3d12 | metal
```

## Keyboard shortcuts

| Shortcut | Action | Shortcut | Action |
|---|---|---|---|
| Ctrl+N / Ctrl+O | New / Open file | Ctrl+Shift+B | Build |
| Ctrl+Shift+O | Open folder | F5 | Build and run |
| Ctrl+S / Ctrl+Shift+S | Save / Save all | Shift+F5 | Stop |
| Ctrl+F / Ctrl+H | Find / Replace | Ctrl+Shift+F | Find in Files |
| Ctrl+P | Go to file | Ctrl+G | Go to line |
| Ctrl+/ | Toggle comment | Alt+Up/Down | Move line |
| Ctrl+Backtick | Toggle terminal | Ctrl+Shift+Backtick | New terminal |
| Ctrl+Shift+V | Markdown preview | Ctrl +/- / 0 | Zoom |

Inside a terminal: `Ctrl+Shift+C` / `Ctrl+Shift+V` copy / paste (also `Ctrl+C` when text is selected,
and `Ctrl+V` on Windows; `Cmd+C` / `Cmd+V` on macOS), `Shift+PageUp/Down` scroll back. Everything else goes to the shell.

## Architecture

```
src/
  main.cpp            startup, GPU/RHI switches, command line, app icon
  MainWindow          docks, menus, toolbar, tabs, project tree, sessions, build wiring
  CodeEditor          QPlainTextEdit based editor + gutter + editing helpers
  SyntaxHighlighter   built-in fallback highlighter (incl. Markdown)
  ThemeManager        Light / Dark Grey palettes + global flat stylesheet
  BuildManager        CMake and .NET CLI driver, diagnostic parser, runner
  PtyProcess          pseudo terminal: ConPTY (Windows) / forkpty (Linux, macOS)
  TerminalScreen      VT100/xterm emulator (grid, scrollback, SGR colours, alt screen)
  TerminalView        terminal widget: painting, keyboard, mouse selection
  TerminalPanel       tabbed set of terminals, shell detection
  FindBar, SearchPanel, QuickOpenDialog, SettingsDialog
scripts/              setup.sh (Linux/macOS), setup.ps1 + setup.cmd (Windows): install, build, run
resources/            logo (SVG), icons (PNG/ICO/ICNS), .desktop file, Windows .rc
tools/make_icons.py   regenerates the raster icons
```

## Notes and limits

* **GPU rendering:** Qt Widgets draws glyphs with `QPainter` on the CPU; CrucibleCode enables Qt's widgets-on-RHI
  compositing so the window is composed and presented by the GPU. Use `--no-gpu` on problematic drivers/VMs.
* **Terminal:** a practical xterm subset (no mouse reporting, no wide-character cell widths, no IME yet).
  Full-screen apps such as vim, htop, less and the PowerShell prompt work.
* **C#:** editing, highlighting and `dotnet` build/run are included; IntelliSense and debugging are on the roadmap.
* **Licensing:** CrucibleCode is MIT. Qt 6 is LGPL v3 (dynamic linking; ship its license texts). QScintilla was
  deliberately not used (GPL/commercial). KSyntaxHighlighting is MIT. See `THIRD_PARTY.md`.

## Roadmap

* Debugger via the Debug Adapter Protocol (GDB / LLDB / vsdbg / netcoredbg)
* Language Server Protocol client (clangd, OmniSharp / csharp-ls): completion, diagnostics, go to definition
* CMake presets and File API (per-target run / debug)
* Multi-cursor, code folding, minimap, Git integration, plugin system
