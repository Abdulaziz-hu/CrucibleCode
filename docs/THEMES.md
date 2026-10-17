# Themes and the Theme Marketplace

CrucibleCode ships with **23 themes** and can install more from the community
[CrucibleCode-Themes](https://github.com/Abdulaziz-hu/CrucibleCode-Themes) repository.

## Using themes

| What | Where |
|------|-------|
| Pick a theme | **Tools > Settings > General > Theme**, or **View > Theme** (More Themes lists the built-in and installed ones) |
| Browse, install and apply community themes | **View > Theme > Theme Marketplace...** or **Settings > General > Theme Marketplace...** |
| Edit every colour yourself | **Customize...** (Custom Theme) |

The **Theme Marketplace** window has three tabs:

* **Marketplace**: every theme in the repository, downloaded live (the last list is cached, so the window also opens offline).
  Select a theme to see a live preview of it *before* installing.
* **Installed**: the themes on your computer, including ones you added from a file.
* **Built-in**: the themes that ship with CrucibleCode.

| Button | What it does |
|--------|--------------|
| **Install** / **Uninstall** | Download a theme to / remove it from your computer. Uninstalling the theme in use switches back first. |
| **Apply** | Use the theme now. A theme that is not installed yet is installed first. Double-clicking a theme also applies it. |
| **Unapply** | Stop using it and return to the theme you used before (Dark Grey if there is none). |
| **Update to x.y.z** | Appears when the repository has a newer version of an installed theme. |
| **Install from File...** | Add any `theme-name.css` from your computer. |
| **Open Themes Folder** | Opens the folder installed themes live in. Dropping a `.css` file in there installs it too. |
| **Submit Your Theme...** | Opens the repository, where themes are shared with a pull request. |

Installed themes are stored in the application data folder (`<AppData>/CrucibleCode/CrucibleCode/themes`,
for example `~/.local/share/CrucibleCode/CrucibleCode/themes` on Linux).

## Making and sharing a theme

1. **Customize...** a theme to your taste (Settings > General > Theme > Customize, you can start from any built-in or installed theme).
2. Click **Export...** and choose *Theme CSS file for the Theme Marketplace (\*.css)*.
3. Open the CrucibleCode-Themes repository and follow its `CONTRIBUTING.md` (add the file to `themes/`, add an entry to `index.json`, open a pull request).

You can also write the file by hand, see below.

## The theme file format

A theme is a plain `.css` file: a header comment with the metadata, and one `:root` block of CSS variables.

```css
/*
 * @name        Crucible Ember
 * @author      CrucibleCode
 * @version     1.0.0
 * @type        dark
 * @description Warm forge-fire tones on a charcoal base.
 * @credits     (optional) Palette inspired by ...
 */

:root {
  /* Interface */
  --window: #231f1c;          /* main window and dock background       */
  --panel: #2c2723;           /* menus, toolbars, tab strip, headers   */
  --base: #1b1816;            /* editor and list background            */
  --text: #e8e0d6;
  --text-dim: #8c8680;
  --border: #4a4540;
  --accent: #c2410c;          /* status bar, focus, active tab marker  */
  --accent-text: #ffffff;     /* text on the accent colour             */
  --hover: #3f3a35;
  --pressed: #773418;
  --selection: #5b3a26;
  --scroll-handle: #524d49;

  /* Editor */
  --line-highlight: #262321;
  --gutter-bg: #1b1816;
  --gutter-fg: #7b7670;
  --gutter-active: #c9c2b9;
  --find-match: #755b2c;

  /* Syntax */
  --keyword: #ff8a3d;  --type: #f2c14e;     --string: #a9cf84;  --number: #e8907a;
  --comment: #84766a;  --preproc: #cba3e6;  --function: #f6dc9a;

  /* Status */
  --error: #ff6b5e;  --warning: #f2b84b;  --success: #8fcf7e;
  --git-added: #8fcf7e;  --git-modified: #5fb3c9;  --git-deleted: #ff6b5e;

  /* Options (optional) */
  --radius: 0px;              /* corner radius of buttons and inputs, 0px to 10px */
  --ui-font-size: 9pt;        /* interface font size, 6pt to 24pt                 */
}
```

* **Colours** can be `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()` / `rgba()` or a CSS colour name. Hex is preferred.
* **`@type`** is `dark` or `light`. It decides which built-in theme fills in any variable your file leaves out
  (the Marketplace repository requires all of them to be present).
* **Only these variables are read.** CrucibleCode never applies anything else from the file: no selectors, no `url()`,
  no raw style sheet. A theme can recolour the IDE, nothing more. Unknown variables are ignored.
* A theme must at least define `--window`, `--base` and `--text`.
* Files larger than 256 KB are rejected.

### Built-in themes

The built-in themes live in `resources/themes/*.css` and use exactly the same format. They are generated from compact
palettes by `tools/make_themes.py`, which also checks readability (contrast) and nudges colours that would be too faint:

```
python3 tools/make_themes.py          # regenerate resources/themes/*.css
python3 tools/make_themes.py --check  # only print the contrast report
```

Rebuild afterwards: the files are embedded in the executable.

## For development and forks

The marketplace address can be changed with the `CRUCIBLE_THEMES_URL` environment variable: an `http(s)` URL of a
repository's raw files, or a plain local folder. That makes it easy to test a theme and its `index.json` entry before
opening a pull request:

```
# Linux / macOS
CRUCIBLE_THEMES_URL=/home/me/CrucibleCode-Themes ./CrucibleCode

# Windows (cmd)
set CRUCIBLE_THEMES_URL=C:\Users\me\CrucibleCode-Themes
CrucibleCode.exe
```

(The `themes/marketplaceUrl` entry in the settings does the same.) The built-in default is
`https://raw.githubusercontent.com/Abdulaziz-hu/CrucibleCode-Themes/main` (see `src/AppInfo.h`).

Everything downloaded is validated: ids must be `[a-z0-9-]`, files always live at `themes/<id>.css`, downloads are
size-limited, and a theme file that does not parse is never installed.
