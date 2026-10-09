#include "ThemeManager.h"

#include <QApplication>
#include <QPalette>
#include <QPair>
#include <QList>

ThemeManager& ThemeManager::instance()
{
    static ThemeManager tm;
    return tm;
}

QString ThemeManager::idToName(Id id)
{
    return id == Light ? QStringLiteral("light") : QStringLiteral("dark");
}

ThemeManager::Id ThemeManager::nameToId(const QString& name, Id fallback)
{
    const QString n = name.toLower();
    if (n == QLatin1String("light")) return Light;
    if (n == QLatin1String("dark") || n == QLatin1String("darkgrey")) return DarkGrey;
    return fallback;
}

ThemeColors ThemeManager::makeColors(Id id)
{
    ThemeColors c;
    if (id == DarkGrey) {
        c.window = "#252526";  c.panel = "#2D2D30";  c.base = "#1E1E1E";
        c.text = "#DCDCDC";    c.textDim = "#858585"; c.border = "#3F3F46";
        c.accent = "#007ACC";  c.accentText = "#FFFFFF";
        c.hover = "#3E3E42";   c.pressed = "#094771"; c.selection = "#264F78";
        c.scrollHandle = "#4B4B4F";
        c.lineHighlight = "#282828"; c.gutterBg = "#1E1E1E";
        c.gutterFg = "#6E7681";      c.gutterActive = "#C6C6C6";
        c.keyword = "#569CD6"; c.type = "#4EC9B0"; c.string = "#D69D85";
        c.number = "#B5CEA8";  c.comment = "#57A64A"; c.preproc = "#BD63C5";
        c.function = "#DCDCAA";
        c.error = "#F48771";   c.warning = "#CCA700"; c.success = "#73C991";
    } else {
        c.window = "#EEEEF2";  c.panel = "#F5F5F5";  c.base = "#FFFFFF";
        c.text = "#1E1E1E";    c.textDim = "#717171"; c.border = "#CCCEDB";
        c.accent = "#007ACC";  c.accentText = "#FFFFFF";
        c.hover = "#DCE9F7";   c.pressed = "#B7D4F0"; c.selection = "#ADD6FF";
        c.scrollHandle = "#C2C3C9";
        c.lineHighlight = "#F2F6FC"; c.gutterBg = "#FFFFFF";
        c.gutterFg = "#2B91AF";      c.gutterActive = "#1E1E1E";
        c.keyword = "#0000FF"; c.type = "#2B91AF"; c.string = "#A31515";
        c.number = "#098658";  c.comment = "#008000"; c.preproc = "#AF00DB";
        c.function = "#74531F";
        c.error = "#E51400";   c.warning = "#BF8803"; c.success = "#388A34";
    }
    return c;
}

void ThemeManager::apply(Id id)
{
    m_id = id;
    m_colors = makeColors(id);
    const ThemeColors& c = m_colors;

    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.panel);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.panel);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.text);
    p.setColor(QPalette::ToolTipBase, c.panel);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.textDim);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Light, c.border);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border);
    p.setColor(QPalette::Shadow, c.border);
    p.setColor(QPalette::Disabled, QPalette::Text, c.textDim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDim);
    qApp->setPalette(p);
    qApp->setStyleSheet(buildStyleSheet());

    emit themeChanged();
}

QString ThemeManager::buildStyleSheet() const
{
    const ThemeColors& c = m_colors;
    QString s = QStringLiteral(R"QSS(
* { border-radius: 0px; }
QMainWindow, QDialog { background: %window%; color: %text%; }
QWidget { color: %text%; }
QMainWindow::separator { background: %border%; width: 1px; height: 1px; }
QSplitter::handle { background: %border%; }
QToolTip { background: %panel%; color: %text%; border: 1px solid %border%; padding: 2px 4px; }

/* ---- menus ---- */
QMenuBar { background: %panel%; border-bottom: 1px solid %border%; padding: 0px; }
QMenuBar::item { background: transparent; padding: 3px 9px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: %hover%; }
QMenu { background: %panel%; border: 1px solid %border%; padding: 1px; }
QMenu::item { padding: 3px 28px 3px 22px; border: 1px solid transparent; }
QMenu::item:selected { background: %accent%; color: %accentText%; }
QMenu::item:disabled { color: %textDim%; background: transparent; }
QMenu::separator { height: 1px; background: %border%; margin: 2px 0px; }

/* ---- toolbars / buttons ---- */
QToolBar { background: %panel%; border: 0px; border-bottom: 1px solid %border%; spacing: 2px; padding: 1px 2px; }
QToolBar::separator { background: %border%; width: 1px; margin: 3px 4px; }
QToolButton { background: transparent; border: 1px solid transparent; padding: 2px 7px; }
QToolButton:hover { background: %hover%; border-color: %border%; }
QToolButton:pressed, QToolButton:checked { background: %pressed%; border-color: %accent%; }
QToolButton:disabled { color: %textDim%; }
QPushButton { background: %panel%; border: 1px solid %border%; padding: 3px 12px; min-height: 18px; }
QPushButton:hover { background: %hover%; border-color: %accent%; }
QPushButton:pressed { background: %pressed%; }
QPushButton:default { border-color: %accent%; }
QPushButton:disabled { color: %textDim%; }

/* ---- inputs ---- */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox {
    background: %base%; border: 1px solid %border%; padding: 1px 4px; min-height: 18px;
    selection-background-color: %selection%; selection-color: %text%; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: %accent%; }
QComboBox QAbstractItemView { background: %panel%; border: 1px solid %border%;
    selection-background-color: %accent%; selection-color: %accentText%; outline: 0; }
QCheckBox { spacing: 5px; }

/* ---- editor & views ---- */
QPlainTextEdit, QTextEdit { background: %base%; border: 1px solid %border%; selection-background-color: %selection%; }
QPlainTextEdit#codeEditor { border: 0px; }
QTreeView, QTreeWidget, QTableView, QListView { background: %base%; border: 1px solid %border%; outline: 0; alternate-background-color: %base%; }
QTreeView::item, QTreeWidget::item { padding: 1px 0px; border: 0px; }
QTreeView::item:hover, QTreeWidget::item:hover { background: %hover%; }
QTreeView::item:selected, QTreeWidget::item:selected { background: %selection%; color: %text%; }
QTreeView::item:selected:!active, QTreeWidget::item:selected:!active { background: %hover%; color: %text%; }
QHeaderView::section { background: %panel%; border: 0px; border-right: 1px solid %border%; border-bottom: 1px solid %border%; padding: 2px 6px; }

/* ---- tabs ---- */
QTabWidget::pane { border: 0px; border-top: 1px solid %border%; }
QTabBar { background: %window%; }
QTabBar::tab { background: %panel%; border: 1px solid %border%; border-bottom: 0px; border-top: 2px solid transparent; padding: 3px 10px; margin-right: -1px; }
QTabBar::tab:hover { background: %hover%; }
QTabBar::tab:selected { background: %base%; border-top: 2px solid %accent%; }
QTabBar::tab:!selected { color: %textDim%; }
QTabBar::tab:bottom { border-top: 0px; border-bottom: 2px solid transparent; }
QTabBar::tab:bottom:selected { border-bottom: 2px solid %accent%; }

/* ---- docks ---- */
QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: %panel%; border: 1px solid %border%; padding: 3px 6px; text-align: left; }

/* ---- scrollbars ---- */
QScrollBar:vertical { background: %window%; width: 13px; margin: 0px; border: 0px; }
QScrollBar:horizontal { background: %window%; height: 13px; margin: 0px; border: 0px; }
QScrollBar::handle:vertical { background: %scrollHandle%; min-height: 24px; margin: 0px 2px; }
QScrollBar::handle:horizontal { background: %scrollHandle%; min-width: 24px; margin: 2px 0px; }
QScrollBar::handle:hover { background: %textDim%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- status bar ---- */
QStatusBar { background: %accent%; color: %accentText%; }
QStatusBar::item { border: 0px; }
QStatusBar QLabel { color: %accentText%; padding: 0px 8px; }
QStatusBar QSizeGrip { width: 0px; height: 0px; }
)QSS");

    const QList<QPair<QString, QColor>> map = {
        {"window", c.window}, {"panel", c.panel}, {"base", c.base}, {"text", c.text},
        {"textDim", c.textDim}, {"border", c.border}, {"accentText", c.accentText},
        {"accent", c.accent}, {"hover", c.hover}, {"pressed", c.pressed},
        {"selection", c.selection}, {"scrollHandle", c.scrollHandle},
    };
    for (const auto& kv : map)
        s.replace(QLatin1Char('%') + kv.first + QLatin1Char('%'), kv.second.name());
    return s;
}
