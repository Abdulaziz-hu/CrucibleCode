# Changelog

Repository: https://github.com/Abdulaziz-hu/CrucibleCode

## 0.9.0
- **Theme Marketplace** (View > Theme, Settings > General): community themes are fetched from https://github.com/Abdulaziz-hu/CrucibleCode-Themes (`index.json` + `themes/<id>.css`). Search, filter dark / light, **live preview** before installing, **Install / Uninstall / Apply / Unapply / Update**, install from a `.css` file, open the themes folder. The list is cached for offline use
- **21 new built-in themes**: Crucible Ember, Midnight Blue, Forest Night, Dracula, Nord, Monokai, One Dark, Tokyo Night, Catppuccin Mocha / Latte, Gruvbox Dark / Light, Solarized Dark / Light, Rosé Pine, GitHub Dark / Light, Paper Sepia, Arctic Light, High Contrast Dark / Light (`tools/make_themes.py` generates them with a readability check)
- **Themes are CSS files**: a metadata comment plus `:root { --window: #...; }` variables, see `docs/THEMES.md`. Only colour variables are read (nothing else in a file is ever applied), so shared themes are safe
- Custom Theme editor: **Export** writes a `theme-name.css` ready for a pull request, **Import** reads `.css` as well as `.cctheme`, and any built-in or installed theme can be the starting point
- Settings > General: theme list with built-in and installed sections; View > Theme > More Themes; the Marketplace has a configurable shortcut (Settings > Shortcuts)
Typical idle footprint (project + a few files open, Linux): **~205 MB -> ~70 MB resident**.
- **GPU compositing is now opt-in** (Settings > General > Graphics, `--gpu`, `--gpu-backend=`): the driver stack cost ~100 MB; `--no-gpu` still forces it off
- **Spell checker**: the word list is a sorted, uncompressed resource searched in place instead of 163k hashed strings (-24 MB heap, identical results, faster lookups); `tools/make_dictionary.py` now writes that format
- **Terminal scrollback** stores lines without trailing blanks (~3x smaller, identical rendering)
- **Code suggestions**: the word list is built on demand and released when the editor loses focus, no full-document copy
- **Opening files** no longer highlights the text three times (old highlighter, clear, new highlighter) and frees the raw bytes before decoding
- **Image viewer** keeps one copy of the pixels instead of two
- **Allocator**: glibc arenas capped, large blocks via mmap, freed memory returned to the OS after closing tabs, on focus loss and once a minute

## 0.8.0
- **Git status in the Explorer**: coloured names with **M / A / U / D / R / !** badges, coloured dots on folders that contain changes, status dots on editor tabs, tooltips, and Git actions (Open Changes, Stage, Unstage, Discard) in the Explorer context menu; can be switched off in Settings

## 0.7.0
- **Packaging scripts**: `build-linux.sh` (.deb, .rpm, .AppImage) and `build-windows.ps1` / `.cmd` (portable ZIP, installer, MSI), each with SHA-256 files
- **Custom Theme**: new entry in the theme list, editor for every colour, corner radius and UI font size, live preview, import / export of `.cctheme` files
- **Project templates**: File > New Project, built-in C / C++ / Qt / C# / web templates, create templates from any folder, export / import, default template (optionally always used)
- **Zoom is global**: one zoom level for all open tabs, new files and future sessions
- **Shortcut manager**: Settings > Shortcuts lists every command; rebind, unbind, reset, conflict detection, import / export
- **Updater** on the GitHub Releases API: release-notes popup with a manual Update button, checksum-verified download, helper that replaces the files and restarts; can be switched off
- **LSP client**: diagnostics, completion, go to definition, hover; configurable servers (clangd and others), Problems panel, status bar summary
- **Performance monitor**: CPU, RAM and thread usage in the status bar (optional)
- **Snippets**: custom triggers, tab stops, mirrors and variables, snippet manager, built-in C++ / Qt / C# / Python / HTML snippets
- Fixed: the Find bar close button was empty (it is now a painted icon that follows the theme)
- Also: tabbed Settings dialog, trim trailing whitespace / final newline on save, Ctrl+, opens Settings, `Problems` dock

## 0.6.0
- Zoom is tracked by the editor itself: theme switches, closing Settings and word wrap no longer reset it
- Find bar: arrow to expand Replace, history dropdowns, live highlighting of all matches, "3 of 12" counter, Esc closes
- Source Control tab (Git): branches, stage / unstage / discard, commit, fetch / pull / push, recent commits, diff tabs, gutter markers, status bar branch
- Repository URL in README, docs, setup scripts, Help menu and About
- About: short disclaimer (hobby project for fun and learning, AI used in certain areas)

## 0.5.0
- Live preview server for HTML / CSS / JS ("Go Live"), auto-closing tags, caret stays solid while typing

## 0.4.0
- Bundled KSyntaxHighlighting, image / PDF viewers, Run from the context menu, Markdown Preview button,
  Shift + wheel scrolling, code suggestions, optional local AI completion, spell check

## 0.3.0 and earlier
- Integrated terminals, per-project tabs, Markdown, C# support, hidden files, startup items, setup scripts, logo
