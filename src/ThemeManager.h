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

// Owns the active theme (Light / Dark Grey), the application palette and the
// global flat, 1px-border, square-corner style sheet.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum Id { Light, DarkGrey, Custom };

    static ThemeManager& instance();

    Id current() const { return m_id; }
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
    bool importTheme(const QString& path, ThemeColors* out, QString* name, QString* error = nullptr) const;
    void preview(const ThemeColors& c);            // apply temporarily, nothing is saved

public slots:
    void apply(ThemeManager::Id id);

signals:
    void themeChanged();

private:
    ThemeManager() = default;
    static ThemeColors makeColors(Id id);
    void applyColors();
    QString buildStyleSheet() const;

    Id m_id = DarkGrey;
    ThemeColors m_colors = makeColors(DarkGrey);
    QFont m_baseFont;
    bool m_haveBaseFont = false;
};
