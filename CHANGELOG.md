# Changelog

Repository: https://github.com/Abdulaziz-hu/CrucibleCode

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
