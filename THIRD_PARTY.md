# Third-party components

| Component | License | How it is used |
|-----------|---------|----------------|
| Qt 6 (Core, Gui, Widgets, Network, optional Svg / Pdf) | LGPL-3.0 / GPL-3.0 / commercial | Dynamically linked UI framework. Obtain from https://www.qt.io |
| KSyntaxHighlighting (KDE Frameworks 6) | MIT (library). Syntax definition files are mostly MIT; a few carry other permissive licenses, see `data/syntax` in its repository | Built from source as a static library into CrucibleCode (`CRUCIBLE_BUNDLE_KSYNTAX`), or the installed system copy |
| extra-cmake-modules | BSD-3-Clause | Build-time only, needed to build KSyntaxHighlighting |
| SCOWL word list (en_US) | Permissive SCOWL license, see `resources/dict/COPYRIGHT-SCOWL.txt` | Bundled spelling dictionary (expanded from the Hunspell en_US data with `tools/make_dictionary.py`) |
| CMake | BSD-3-Clause | Invoked as an external process for project builds |
| aqtinstall | MIT | Used only by the setup scripts to download Qt from the official Qt servers |
| llama.cpp (`llama-server`) | MIT | Optional, never bundled: downloaded on request for AI tab completion, or taken from your PATH |
| Qwen2.5-Coder 0.5B / 1.5B (GGUF) | Apache-2.0 | Optional, never bundled: downloaded on request from Hugging Face for AI tab completion |

No proprietary or commercial-only components are required to build or run CrucibleCode.
