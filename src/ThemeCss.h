#pragma once
#include "ThemeManager.h"

#include <QString>
#include <QStringList>

// Reader / writer for CrucibleCode theme files (`theme-name.css`).
//
//   /*
//    * @name        Crucible Ember
//    * @author      CrucibleCode
//    * @version     1.0.0
//    * @type        dark
//    * @description Warm forge-fire tones.
//    */
//   :root {
//     --window: #231f1c;
//     --line-highlight: #262321;
//     ...
//     --radius: 2px;            (optional)
//     --ui-font-size: 9pt;      (optional)
//   }
//
// Only these colour variables are read. Nothing else in the file is ever applied to the application
// (no selectors, no url(), no raw style sheet), so a theme can never do more than recolour the UI.
// The format is documented in docs/THEMES.md and in the CrucibleCode-Themes repository.
namespace ThemeCss {

struct Meta {
    QString name, author, version, type, description, credits;   // type: "dark" or "light"
};

struct Result {
    bool ok = false;
    QString error;
    Meta meta;
    ThemeColors colors;
    QStringList missing;   // colour variables that are not in the file (taken from the Dark Grey / Light base)
    QStringList unknown;   // variables that are not part of the format (ignored)
};

// "#rgb", "#rgba", "#rrggbb", "#rrggbbaa", "rgb(...)", "rgba(...)" or a CSS colour name.
QColor parseColor(const QString& text, bool* ok = nullptr);
// "#rrggbb", or "#rrggbbaa" when the colour is translucent.
QString colorToCss(const QColor& c);

// lineHighlight -> line-highlight
QString keyToVariable(const QString& key);

Result parse(const QString& css);
QString serialize(const ThemeColors& colors, const Meta& meta);

// Size limit for theme files (they are tiny; this protects against junk downloads).
constexpr int kMaxFileBytes = 256 * 1024;

}   // namespace ThemeCss
