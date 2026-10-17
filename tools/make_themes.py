#!/usr/bin/env python3
"""
Generates the built-in CrucibleCode themes (resources/themes/*.css) from compact palettes.

    python3 tools/make_themes.py                 # writes resources/themes/*.css and prints a contrast report
    python3 tools/make_themes.py --check         # only prints the report

Every theme is written in the public CrucibleCode theme format (see docs/THEMES.md): a metadata header
comment followed by CSS custom properties in :root. The same format is used by the community themes in
https://github.com/Abdulaziz-hu/CrucibleCode-Themes

A palette only has to provide the "anchor" colours (window / panel / base / text / accent, the seven
syntax colours and the three status colours). Everything else (borders, hover, selection, gutter, ...)
is derived by mixing, unless a palette overrides it. Run the report to see readability problems.
"""
import argparse
import os
import sys

# ----------------------------------------------------------------------------- colour helpers


def rgb(c):
    c = c.lstrip("#")
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))


def hexs(t):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v))) for v in t)


def mix(a, b, t):
    """t = 0 -> a, t = 1 -> b"""
    ra, rb = rgb(a), rgb(b)
    return hexs(tuple(ra[i] + (rb[i] - ra[i]) * t for i in range(3)))


def luminance(c):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = rgb(c)
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


# ----------------------------------------------------------------------------- format

GROUPS = [
    ("Interface", [("window", "window"), ("panel", "panel"), ("base", "base"), ("text", "text"),
                   ("textDim", "text-dim"), ("border", "border"), ("accent", "accent"),
                   ("accentText", "accent-text"), ("hover", "hover"), ("pressed", "pressed"),
                   ("selection", "selection"), ("scrollHandle", "scroll-handle")]),
    ("Editor", [("lineHighlight", "line-highlight"), ("gutterBg", "gutter-bg"), ("gutterFg", "gutter-fg"),
                ("gutterActive", "gutter-active"), ("findMatch", "find-match")]),
    ("Syntax", [("keyword", "keyword"), ("type", "type"), ("string", "string"), ("number", "number"),
                ("comment", "comment"), ("preproc", "preproc"), ("function", "function")]),
    ("Status", [("error", "error"), ("warning", "warning"), ("success", "success"),
                ("gitAdded", "git-added"), ("gitModified", "git-modified"), ("gitDeleted", "git-deleted")]),
]
ALL_KEYS = [k for _, items in GROUPS for k, _ in items]


def derive(p):
    """Fill in every colour a palette did not specify."""
    c = dict(p)
    dark = p["kind"] == "dark"
    w, pa, ba, tx, ac = c["window"], c["panel"], c["base"], c["text"], c["accent"]
    if dark:
        d = dict(border=mix(pa, tx, .16), textDim=mix(ba, tx, .55), hover=mix(pa, tx, .10),
                 pressed=mix(pa, ac, .50), selection=mix(ba, ac, .38), scrollHandle=mix(w, tx, .24),
                 lineHighlight=mix(ba, tx, .055), gutterFg=mix(ba, tx, .47), gutterActive=mix(ba, tx, .85),
                 findMatch=mix(ba, c["warning"], .42))
    else:
        d = dict(border=mix(pa, tx, .20), textDim=mix(w, tx, .68), hover=mix(w, ac, .16),
                 pressed=mix(w, ac, .32), selection=mix(ba, ac, .26), scrollHandle=mix(w, tx, .20),
                 lineHighlight=mix(ba, ac, .06), gutterFg=mix(ba, tx, .60), gutterActive=tx,
                 findMatch=mix(ba, c["warning"], .50))
    d["gutterBg"] = ba
    # text on the accent colour (status bar, menu selection): the better of white and the dark colour
    dark_ink = ba if dark else "#111111"
    d["accentText"] = "#ffffff" if contrast("#ffffff", ac) >= contrast(dark_ink, ac) else dark_ink
    d["gitAdded"] = c["success"]
    d["gitModified"] = c.get("info", c["function"])
    d["gitDeleted"] = c["error"]
    for k, v in d.items():
        c.setdefault(k, v)
    return c



def nudge(color, bg, minimum, toward):
    """Move `color` towards `toward` (white or black) in small steps until it reaches `minimum` contrast on `bg`."""
    if contrast(color, bg) >= minimum:
        return color
    for i in range(1, 51):
        c = mix(color, toward, i / 50)
        if contrast(c, bg) >= minimum:
            return c
    return toward


def enforce(p, c):
    """Guarantee readable text: nudges colours that fall below the thresholds. Returns notes."""
    notes = []
    dark = p["kind"] == "dark"
    ink = "#ffffff" if dark else "#000000"      # direction that increases contrast against the base
    base, window = c["base"], c["window"]

    def fix(key, bg, minimum, toward=ink):
        old = c[key]
        c[key] = nudge(old, c[bg], minimum, toward)
        if c[key] != old:
            notes.append("%s %s -> %s" % (key, old, c[key]))

    fix("text", "base", 5.0)
    for k in ("keyword", "type", "string", "number", "preproc", "function"):
        fix(k, "base", 4.5)
    for k in ("error", "warning", "success"):
        fix(k, "base", 4.0)
    fix("comment", "base", 3.0)
    fix("textDim", "base", 3.0)
    fix("textDim", "window", 3.0)
    fix("gutterFg", "base", 3.0)
    # backgrounds the text is drawn on: move them back towards the base colour
    for k in ("selection", "hover", "lineHighlight"):
        old = c[k]
        c[k] = nudge(old, c["text"], 4.5, base)
        if c[k] != old:
            notes.append("%s %s -> %s" % (k, old, c[k]))
    # status bar / menu selection: text on the accent colour
    best_ink = "#ffffff" if contrast("#ffffff", c["accent"]) >= contrast(c["accentText"], c["accent"]) else c["accentText"]
    c["accentText"] = best_ink
    if contrast(c["accentText"], c["accent"]) < 4.5:
        old = c["accent"]
        c["accent"] = nudge(old, c["accentText"], 4.5, "#000000" if c["accentText"] == "#ffffff" else "#ffffff")
        notes.append("accent %s -> %s" % (old, c["accent"]))
    return notes


def render(p, c):
    out = ["/*",
           " * @name        %s" % p["name"],
           " * @author      %s" % p.get("author", "CrucibleCode"),
           " * @version     %s" % p.get("version", "1.0.0"),
           " * @type        %s" % p["kind"],
           " * @description %s" % p["desc"]]
    if p.get("credits"):
        out.append(" * @credits     %s" % p["credits"])
    out += [" */", "", ":root {"]
    for gi, (gname, items) in enumerate(GROUPS):
        if gi:
            out.append("")
        out.append("  /* %s */" % gname)
        for key, var in items:
            out.append("  --%s: %s;" % (var, c[key]))
    if "radius" in p:
        out += ["", "  /* Options */", "  --radius: %dpx;" % p["radius"]]
    out += ["}", ""]
    return "\n".join(out)


def report(p, c):
    """Readability checks. Returns a list of (severity, message)."""
    issues = []

    def need(label, fg, bg, minimum, severity="WARN"):
        r = contrast(c[fg], c[bg])
        if r < minimum:
            issues.append((severity, "%-14s on %-12s %.2f (< %.1f)" % (fg, bg, r, minimum)))

    need("text", "text", "base", 5.0)
    need("text", "text", "window", 4.5)
    need("text", "text", "panel", 4.5)
    need("text", "text", "selection", 4.5)
    need("text", "text", "hover", 4.5)
    need("text", "text", "lineHighlight", 4.5)
    need("textDim", "textDim", "base", 3.0)
    need("textDim", "textDim", "window", 3.0)
    need("accentText", "accentText", "accent", 4.5)
    for k in ("keyword", "type", "string", "number", "preproc", "function"):
        need(k, k, "base", 4.5)
    need("comment", "comment", "base", 3.0)
    for k in ("error", "warning", "success"):
        need(k, k, "base", 4.0)
    need("gutterFg", "gutterFg", "base", 3.0)
    return issues


# ----------------------------------------------------------------------------- built-in palettes


def T(id_, name, type_, desc, **kw):
    d = dict(id=id_, name=name, kind=type_, desc=desc)
    d.update(kw)
    return d


BUILTIN = [
    # ---------------------------------------------------------------- dark
    T("crucible-ember", "Crucible Ember", "dark", "Warm forge-fire tones on a charcoal base. The signature CrucibleCode look.",
      window="#231f1c", panel="#2c2723", base="#1b1816", text="#e8e0d6", accent="#c2410c",
      keyword="#ff8a3d", type="#f2c14e", string="#a9cf84", number="#e8907a", comment="#84766a",
      preproc="#cba3e6", function="#f6dc9a", error="#ff6b5e", warning="#f2b84b", success="#8fcf7e",
      selection="#5b3a26", info="#5fb3c9"),
    T("midnight-blue", "Midnight Blue", "dark", "Deep navy with cool blue and teal highlights. Easy on the eyes at night.",
      window="#0f172a", panel="#16213a", base="#0b1220", text="#cbd5e1", accent="#2563eb",
      keyword="#60a5fa", type="#5eead4", string="#fbbf77", number="#a5b4fc", comment="#6b7f9e",
      preproc="#c4a7f5", function="#f9e08a", error="#f87171", warning="#fbbf24", success="#4ade80",
      selection="#1d3b7a", info="#38bdf8"),
    T("forest-night", "Forest Night", "dark", "Dark evergreen background with soft natural colours.",
      window="#15211b", panel="#1c2b24", base="#101a15", text="#d4e2d4", accent="#2f7d57",
      keyword="#8ccf7e", type="#6fd3c0", string="#e6c07b", number="#e59f78", comment="#6b8576",
      preproc="#b79ce0", function="#cfe38a", error="#f0786b", warning="#e6b94d", success="#7ed48f",
      selection="#24493a", info="#6fd3c0"),
    T("dracula", "Dracula", "dark", "Dark purple-grey with vivid pink, green and cyan.",
      credits="Palette inspired by the Dracula theme (MIT), https://draculatheme.com",
      window="#21222c", panel="#2b2d3b", base="#282a36", text="#f8f8f2", accent="#bd93f9",
      keyword="#ff79c6", type="#8be9fd", string="#f1fa8c", number="#bd93f9", comment="#6272a4",
      preproc="#ffb86c", function="#50fa7b", error="#ff5555", warning="#ffb86c", success="#50fa7b",
      selection="#44475a", info="#8be9fd"),
    T("nord", "Nord", "dark", "Arctic, north-bluish palette with calm, low-contrast accents.",
      credits="Palette inspired by Nord (MIT), https://www.nordtheme.com",
      window="#272c36", panel="#333a47", base="#2e3440", text="#d8dee9", accent="#5e81ac",
      accentText="#eceff4",
      keyword="#81a1c1", type="#8fbcbb", string="#a3be8c", number="#b48ead", comment="#6c7a96",
      preproc="#d08770", function="#88c0d0", error="#bf616a", warning="#ebcb8b", success="#a3be8c",
      selection="#434c5e", info="#88c0d0"),
    T("monokai", "Monokai", "dark", "The classic warm dark scheme with a hot pink and lime palette.",
      credits="Palette inspired by Monokai by Wimer Hazenberg",
      window="#1e1f1c", panel="#2e2f28", base="#272822", text="#f8f8f2", accent="#a6e22e",
      keyword="#f92672", type="#66d9ef", string="#e6db74", number="#ae81ff", comment="#7e7a63",
      preproc="#fd971f", function="#a6e22e", error="#f92672", warning="#fd971f", success="#a6e22e",
      selection="#49483e", info="#66d9ef"),
    T("one-dark", "One Dark", "dark", "Balanced dark theme with soft pastel syntax colours.",
      credits="Palette inspired by Atom One Dark (MIT)",
      window="#21252b", panel="#2c313a", base="#282c34", text="#abb2bf", accent="#4d78cc",
      keyword="#c678dd", type="#e5c07b", string="#98c379", number="#d19a66", comment="#6b7385",
      preproc="#56b6c2", function="#61afef", error="#e06c75", warning="#e5c07b", success="#98c379",
      selection="#3e4451", info="#61afef"),
    T("tokyo-night", "Tokyo Night", "dark", "A dark theme inspired by the neon lights of downtown Tokyo at night.",
      credits="Palette inspired by Tokyo Night (MIT) by enkia",
      window="#16161e", panel="#1f2335", base="#1a1b26", text="#c0caf5", accent="#3d59a1",
      keyword="#bb9af7", type="#2ac3de", string="#9ece6a", number="#ff9e64", comment="#636da6",
      preproc="#7dcfff", function="#7aa2f7", error="#f7768e", warning="#e0af68", success="#9ece6a",
      selection="#2e3c64", info="#7dcfff"),
    T("catppuccin-mocha", "Catppuccin Mocha", "dark", "Soothing pastel theme, the darkest Catppuccin flavour.",
      credits="Palette inspired by Catppuccin (MIT), https://catppuccin.com",
      window="#181825", panel="#262637", base="#1e1e2e", text="#cdd6f4", accent="#89b4fa",
      keyword="#cba6f7", type="#f9e2af", string="#a6e3a1", number="#fab387", comment="#7f849c",
      preproc="#f5c2e7", function="#89b4fa", error="#f38ba8", warning="#f9e2af", success="#a6e3a1",
      selection="#45475a", info="#89dceb"),
    T("gruvbox-dark", "Gruvbox Dark", "dark", "Retro-groove dark theme with warm, earthy contrast.",
      credits="Palette inspired by Gruvbox (MIT) by Pavel Pertsev",
      window="#1d2021", panel="#32302f", base="#282828", text="#ebdbb2", accent="#0d6b78",
      accentText="#fbf1c7",
      keyword="#fb4934", type="#fabd2f", string="#b8bb26", number="#d3869b", comment="#928374",
      preproc="#8ec07c", function="#83a598", error="#fb4934", warning="#fabd2f", success="#b8bb26",
      selection="#504945", info="#83a598"),
    T("solarized-dark", "Solarized Dark", "dark", "Precision colours for machines and people, dark variant.",
      credits="Palette inspired by Solarized (MIT) by Ethan Schoonover",
      window="#00212b", panel="#073642", base="#002b36", text="#a4b2b2", accent="#1d77b3",
      keyword="#859900", type="#b58900", string="#2aa198", number="#d33682", comment="#66808a",
      preproc="#cb4b16", function="#268bd2", error="#ec5b57", warning="#c99a0a", success="#9bb300",
      selection="#0f4f61", info="#2aa198"),
    T("rose-pine", "Rosé Pine", "dark", "All natural pine, faux fur and a bit of soho vibes.",
      credits="Palette inspired by Rosé Pine (MIT), https://rosepinetheme.com",
      window="#14121f", panel="#1f1d2e", base="#191724", text="#e0def4", accent="#c4a7e7",
      keyword="#c4a7e7", type="#9ccfd8", string="#f6c177", number="#ea9a97", comment="#7b7794",
      preproc="#eb6f92", function="#ebbcba", error="#eb6f92", warning="#f6c177", success="#8bd5b5",
      selection="#403d52", info="#9ccfd8"),
    T("github-dark", "GitHub Dark", "dark", "The familiar GitHub dark palette.",
      credits="Palette inspired by GitHub Primer (MIT)",
      window="#010409", panel="#161b22", base="#0d1117", text="#c9d1d9", accent="#1f6feb",
      border="#30363d",
      keyword="#ff7b72", type="#ffa657", string="#a5d6ff", number="#79c0ff", comment="#8b949e",
      preproc="#7ee787", function="#d2a8ff", error="#f85149", warning="#d29922", success="#3fb950",
      selection="#1f3d66", info="#58a6ff"),
    T("high-contrast-dark", "High Contrast Dark", "dark", "Pure black with bright text and strong borders, for maximum legibility.",
      window="#000000", panel="#0a0a0a", base="#000000", text="#ffffff", accent="#0b63c5",
      textDim="#c8c8c8", border="#8c8c8c", hover="#262626", pressed="#14408a", selection="#1b4f8c",
      scrollHandle="#6a6a6a", lineHighlight="#1a1a1a", gutterFg="#b0b0b0",
      keyword="#6cb6ff", type="#4ec9b0", string="#ffb38a", number="#c3e88d", comment="#7ec27a",
      preproc="#ff7ad9", function="#ffe66d", error="#ff6b6b", warning="#ffd24d", success="#5af78e",
      info="#6cb6ff"),
    # ---------------------------------------------------------------- light
    T("solarized-light", "Solarized Light", "light", "Precision colours for machines and people, light variant.",
      credits="Palette inspired by Solarized (MIT) by Ethan Schoonover",
      window="#eee8d5", panel="#f5efdc", base="#fdf6e3", text="#4f666e", accent="#1d77b3",
      keyword="#667a00", type="#946f00", string="#1d8077", number="#c0286e", comment="#7a8d8f",
      preproc="#b8400e", function="#1a6fb0", error="#d12a27", warning="#9a7400", success="#5f7d00",
      selection="#d6dfe0", info="#1d8077"),
    T("gruvbox-light", "Gruvbox Light", "light", "Retro-groove light theme on warm parchment.",
      credits="Palette inspired by Gruvbox (MIT) by Pavel Pertsev",
      window="#ebdbb2", panel="#f2e5bc", base="#fbf1c7", text="#3c3836", accent="#076678",
      keyword="#9d0006", type="#8f5a0b", string="#6b6a0c", number="#8f3f71", comment="#7c6f64",
      preproc="#36694a", function="#076678", error="#b91c14", warning="#9a600f", success="#6b6a0c",
      selection="#d5c4a1", info="#076678"),
    T("catppuccin-latte", "Catppuccin Latte", "light", "Soothing pastel theme, the lightest Catppuccin flavour.",
      credits="Palette inspired by Catppuccin (MIT), https://catppuccin.com",
      window="#dce0e8", panel="#e6e9ef", base="#eff1f5", text="#4c4f69", accent="#1e66f5",
      keyword="#8839ef", type="#a8620b", string="#2f7d1f", number="#d0510b", comment="#7c7f93",
      preproc="#c0399a", function="#1e66f5", error="#d20f39", warning="#a8620b", success="#2f7d1f",
      selection="#cdd5f0", info="#0b7fb5"),
    T("github-light", "GitHub Light", "light", "The familiar GitHub light palette.",
      credits="Palette inspired by GitHub Primer (MIT)",
      window="#eaeef2", panel="#f6f8fa", base="#ffffff", text="#1f2328", accent="#0969da",
      border="#d0d7de",
      keyword="#cf222e", type="#953800", string="#0a3069", number="#0550ae", comment="#6e7781",
      preproc="#116329", function="#8250df", error="#cf222e", warning="#9a6700", success="#1a7f37",
      selection="#b6e3ff", info="#0969da"),
    T("paper-sepia", "Paper Sepia", "light", "Warm paper tones with ink-like text, gentle on long reading sessions.",
      window="#ece2cc", panel="#f3ead6", base="#f9f3e5", text="#3f362a", accent="#8c5a2b",
      keyword="#a1401d", type="#1f6a73", string="#587a18", number="#8a4a9b", comment="#8a7f6c",
      preproc="#6b4e9b", function="#8a5a00", error="#b3261e", warning="#96620a", success="#3f7d20",
      selection="#e3d2a8", info="#1f6a73"),
    T("arctic-light", "Arctic Light", "light", "Crisp white with cool blue-grey chrome.",
      window="#e6edf5", panel="#f0f4f9", base="#fbfdff", text="#1f2a37", accent="#1d6fd6",
      keyword="#2557d6", type="#0b7285", string="#b4452b", number="#7b3fc4", comment="#6b7c93",
      preproc="#9b2fae", function="#a85d00", error="#d32f2f", warning="#a65f00", success="#1f8a4c",
      selection="#c7dcf8", info="#0b7285"),
    T("high-contrast-light", "High Contrast Light", "light", "Pure white with black text and strong borders, for maximum legibility.",
      window="#ffffff", panel="#f2f2f2", base="#ffffff", text="#000000", accent="#0f4a85",
      textDim="#404040", border="#4d4d4d", hover="#d6e6f7", pressed="#a9cdf2", selection="#99c9ff",
      scrollHandle="#8c8c8c", lineHighlight="#eef4fb", gutterFg="#333333", findMatch="#ffe14d",
      keyword="#0000c8", type="#00618a", string="#a31515", number="#0b6a2f", comment="#476b2a",
      preproc="#8b008b", function="#6b3f00", error="#c00000", warning="#8a5a00", success="#00701a",
      info="#00618a"),
]


def write_all(palettes, outdir, quiet=False):
    os.makedirs(outdir, exist_ok=True)
    problems = 0
    for p in palettes:
        c = derive(p)
        notes = enforce(p, c)
        if notes and not quiet:
            print("  %s: adjusted for readability: %s" % (p["name"], "; ".join(notes)))
        for k in ALL_KEYS:
            assert k in c, "%s: missing %s" % (p["id"], k)
        iss = report(p, c)
        if iss and not quiet:
            print("  %s" % p["name"])
            for sev, msg in iss:
                print("      %s %s" % (sev, msg))
        problems += len(iss)
        if outdir:
            with open(os.path.join(outdir, p["id"] + ".css"), "w", newline="\n") as f:
                f.write(render(p, c))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="only print the contrast report")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "resources", "themes"))
    a = ap.parse_args()
    out = a.out
    if a.check:
        for p in BUILTIN:
            c = derive(p)
            enforce(p, c)
            for sev, msg in report(p, c):
                print("%-20s %s %s" % (p["name"], sev, msg))
        return
    n = write_all(BUILTIN, out)
    print("%d themes written to %s (%d readability notes)" % (len(BUILTIN), os.path.normpath(out), n))


if __name__ == "__main__":
    sys.exit(main())
