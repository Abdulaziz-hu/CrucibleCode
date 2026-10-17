#pragma once
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QFont>
#include <QString>

struct ThemeColors {
    // chrome
    QColor window, panel, base, text, textDim, border, accent, accentText;
    QColor hover, pressed, selection, scrollHandle;
    // editor
    QColor lineHighlight, gutterBg, gutterFg, gutterActive;
    // syntax
    QColor keyword, type, string, number, comment, preproc, function;
    // status colors
    QColor error, warning, success;
    // find matches and Git change markers
    QColor findMatch, gitAdded, gitModified, gitDeleted;
    // non-colour options (custom theme only; the built-in themes keep the defaults)
    int radius = 0;            // corner radius of buttons / inputs / tabs in px (0 = square)
    double uiFontSize = 0;     // UI font size in points (0 = application default)

    struct Field {
        const char* key;       // JSON key
        const char* label;     // shown in the theme editor
        const char* group;
        QColor ThemeColors::* member;
    };
    static const QList<Field>& fields();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);   // missing keys keep their current value
};

// Owns the active theme, the application palette and the global flat, 1px-border style sheet.
//
// A theme is identified by a *key* (this is what the "theme" setting stores):
//   "dark" / "light" / "custom"   native themes (Dark Grey, Light, the editable Custom Theme)
//   "builtin:<id>"                a CSS theme shipped with the application
//   "installed:<id>"              a CSS theme from the user's themes folder / the Theme Marketplace
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum Id { Light, DarkGrey, Custom, Named };   // Named = a CSS theme, see currentKey()

    static ThemeManager& instance();

    Id current() const { return m_id; }
    QString currentKey() const { return m_key; }
    QString currentName() const { return m_name; }
    bool isDark() const;
    const ThemeColors& colors() const { return m_colors; }

    static QString idToName(Id id);
    static Id nameToId(const QString& name, Id fallback = DarkGrey);
    static ThemeColors baseColors(Id id);          // Light or DarkGrey palette

    // Custom theme: stored in the settings, can be exported to / imported from a .cctheme file.
    ThemeColors customColors() const;
    QString customName() const;
    void setCustom(const ThemeColors& c, const QString& name);   // persists (does not apply)
    bool exportCustom(const ThemeColors& c, const QString& name, const QString& path, QString* error = nullptr) const;
    // Writes a shareable `theme-name.css` file (the format used by the Theme Marketplace repository).
    bool exportCss(const ThemeColors& c, const QString& name, const QString& author, const QString& path, QString* error = nullptr) const;
    // Reads a .css theme or a .cctheme / .json file (decided by the extension).
    bool importTheme(const QString& path, ThemeColors* out, QString* name, QString* error = nullptr) const;
    void preview(const ThemeColors& c);            // apply temporarily, nothing is saved

    // Applies the theme with this key and stores it in the "theme" setting. When the theme can no longer be
    // loaded (for example it was uninstalled) Dark Grey is used instead and false is returned.
    bool select(const QString& key);
    bool applyKey(const QString& key);             // same, but nothing is stored

public slots:
    void apply(ThemeManager::Id id);               // native themes only (Named is ignored, use applyKey)

signals:
    void themeChanged();

private:
    ThemeManager() = default;
    static ThemeColors makeColors(Id id);
    void applyColors();
    QString buildStyleSheet() const;

    Id m_id = DarkGrey;
    QString m_key = QStringLiteral("dark");
    QString m_name = QStringLiteral("Dark Grey");
    ThemeColors m_colors = makeColors(DarkGrey);
    QFont m_baseFont;
    bool m_haveBaseFont = false;
};
