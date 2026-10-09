#pragma once
#include <QColor>
#include <QObject>
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
};

// Owns the active theme (Light / Dark Grey), the application palette and the
// global flat, 1px-border, square-corner style sheet.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum Id { Light, DarkGrey };

    static ThemeManager& instance();

    Id current() const { return m_id; }
    bool isDark() const { return m_id == DarkGrey; }
    const ThemeColors& colors() const { return m_colors; }

    static QString idToName(Id id);
    static Id nameToId(const QString& name, Id fallback = DarkGrey);

public slots:
    void apply(ThemeManager::Id id);

signals:
    void themeChanged();

private:
    ThemeManager() = default;
    static ThemeColors makeColors(Id id);
    QString buildStyleSheet() const;

    Id m_id = DarkGrey;
    ThemeColors m_colors = makeColors(DarkGrey);
};
