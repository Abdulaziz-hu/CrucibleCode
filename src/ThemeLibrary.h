#pragma once
#include "ThemeManager.h"

#include <QList>
#include <QString>

// One selectable theme. `key` is what is stored in the "theme" setting:
//   "dark" / "light" / "custom"   the native themes
//   "builtin:<id>"                a CSS theme shipped inside the application (resources/themes)
//   "installed:<id>"              a CSS theme in the user's themes folder (marketplace or imported)
struct ThemeInfo {
    QString key;
    QString id;
    QString name, author, version, description, type, credits;   // type: "dark" / "light"
    QString path;            // CSS file (":/themes/x.css" or a path on disk); empty for the native themes
    bool builtin = false;
    bool local = false;      // installed from a file on disk instead of the marketplace
    bool isValid() const { return !key.isEmpty(); }
};

// Everything about *which themes exist*: the built-in ones, the installed ones and how to
// install / remove a theme. Fetching from the marketplace lives in ThemeMarketplace.
class ThemeLibrary {
public:
    static QString builtinKey(const QString& id) { return QStringLiteral("builtin:") + id; }
    static QString installedKey(const QString& id) { return QStringLiteral("installed:") + id; }

    // <app data>/themes - created on demand. Drop a .css file in here and it shows up as installed.
    static QString themesDir();
    static QString cacheDir();                                   // marketplace downloads

    // Marketplace ids: lower-case letters, digits and hyphens, e.g. "neon-synthwave".
    static bool validMarketplaceId(const QString& id);
    static QString sanitizeId(const QString& text);              // "My Theme!" -> "my-theme"

    static QList<ThemeInfo> builtinThemes();                     // Dark Grey, Light, then the CSS themes (dark, then light)
    static QList<ThemeInfo> installedThemes();                   // sorted by name
    static ThemeInfo installedInfo(const QString& id);           // invalid ThemeInfo when not installed
    static bool isInstalled(const QString& id) { return installedInfo(id).isValid(); }
    static ThemeInfo info(const QString& key);                   // any key; invalid when unknown / not installed
    static bool exists(const QString& key);                      // includes "custom"

    // Installs a downloaded marketplace theme. `meta` comes from the marketplace index and wins over the
    // header inside the file. The file is validated before anything is written.
    static bool install(const ThemeInfo& meta, const QByteArray& css, QString* error = nullptr);
    // Imports a .css file chosen by the user. Returns the installed theme in `out`.
    static bool installFromFile(const QString& path, ThemeInfo* out, QString* error = nullptr);
    static bool uninstall(const QString& id, QString* error = nullptr);

    // Colours for any key (also "light", "dark", "custom").
    static bool load(const QString& key, ThemeColors* colors, QString* error = nullptr);

    // "1.2.0" > "1.10.0"? numeric comparison of dotted versions; -1 / 0 / 1
    static int compareVersions(const QString& a, const QString& b);

private:
    static bool safeFileId(const QString& id);
};
