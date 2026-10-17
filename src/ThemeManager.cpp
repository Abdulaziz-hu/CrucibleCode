#include "ThemeManager.h"
#include "ThemeCss.h"
#include "ThemeLibrary.h"

#include <QApplication>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QPalette>
#include <QPair>
#include <QList>

ThemeManager& ThemeManager::instance()
{
    static ThemeManager tm;
    return tm;
}

const QList<ThemeColors::Field>& ThemeColors::fields()
{
    static const QList<Field> f = {
        {"window", "Window background", "Interface", &ThemeColors::window},
        {"panel", "Panels, menus, toolbars", "Interface", &ThemeColors::panel},
        {"base", "Editor / list background", "Interface", &ThemeColors::base},
        {"text", "Text", "Interface", &ThemeColors::text},
        {"textDim", "Dimmed text", "Interface", &ThemeColors::textDim},
        {"border", "Borders", "Interface", &ThemeColors::border},
        {"accent", "Accent (status bar, focus)", "Interface", &ThemeColors::accent},
        {"accentText", "Text on accent", "Interface", &ThemeColors::accentText},
        {"hover", "Hover", "Interface", &ThemeColors::hover},
        {"pressed", "Pressed / checked", "Interface", &ThemeColors::pressed},
        {"selection", "Selection", "Interface", &ThemeColors::selection},
        {"scrollHandle", "Scrollbar handle", "Interface", &ThemeColors::scrollHandle},
        {"lineHighlight", "Current line", "Editor", &ThemeColors::lineHighlight},
        {"gutterBg", "Line number background", "Editor", &ThemeColors::gutterBg},
        {"gutterFg", "Line numbers", "Editor", &ThemeColors::gutterFg},
        {"gutterActive", "Current line number", "Editor", &ThemeColors::gutterActive},
        {"findMatch", "Find match", "Editor", &ThemeColors::findMatch},
        {"keyword", "Keywords", "Syntax", &ThemeColors::keyword},
        {"type", "Types", "Syntax", &ThemeColors::type},
        {"string", "Strings", "Syntax", &ThemeColors::string},
        {"number", "Numbers", "Syntax", &ThemeColors::number},
        {"comment", "Comments", "Syntax", &ThemeColors::comment},
        {"preproc", "Preprocessor", "Syntax", &ThemeColors::preproc},
        {"function", "Functions", "Syntax", &ThemeColors::function},
        {"error", "Errors", "Status", &ThemeColors::error},
        {"warning", "Warnings", "Status", &ThemeColors::warning},
        {"success", "Success", "Status", &ThemeColors::success},
        {"gitAdded", "Git: added", "Status", &ThemeColors::gitAdded},
        {"gitModified", "Git: modified", "Status", &ThemeColors::gitModified},
        {"gitDeleted", "Git: deleted", "Status", &ThemeColors::gitDeleted},
    };
    return f;
}

QJsonObject ThemeColors::toJson() const
{
    QJsonObject o;
    for (const Field& f : fields()) o.insert(QLatin1String(f.key), (this->*f.member).name());
    o.insert(QStringLiteral("radius"), radius);
    o.insert(QStringLiteral("uiFontSize"), uiFontSize);
    return o;
}

void ThemeColors::fromJson(const QJsonObject& o)
{
    for (const Field& f : fields()) {
        const QColor c(o.value(QLatin1String(f.key)).toString());
        if (c.isValid()) this->*f.member = c;
    }
    if (o.contains(QStringLiteral("radius"))) radius = qBound(0, o.value(QStringLiteral("radius")).toInt(), 10);
    if (o.contains(QStringLiteral("uiFontSize"))) {
        const double d = o.value(QStringLiteral("uiFontSize")).toDouble();
        uiFontSize = (d >= 6 && d <= 24) ? d : 0;
    }
}

QString ThemeManager::idToName(Id id)
{
    return id == Light ? QStringLiteral("light") : id == Custom ? QStringLiteral("custom") : QStringLiteral("dark");
}

bool ThemeManager::isDark() const
{
    if (m_id == Custom || m_id == Named) return m_colors.window.lightness() < 128;
    return m_id == DarkGrey;
}

ThemeColors ThemeManager::baseColors(Id id) { return makeColors(id == Light ? Light : DarkGrey); }

ThemeColors ThemeManager::customColors() const
{
    ThemeColors c = makeColors(DarkGrey);
    const QByteArray json = QSettings().value(QStringLiteral("theme/custom")).toString().toUtf8();
    if (!json.isEmpty()) c.fromJson(QJsonDocument::fromJson(json).object());
    return c;
}

QString ThemeManager::customName() const
{
    return QSettings().value(QStringLiteral("theme/customName"), QStringLiteral("My Theme")).toString();
}

void ThemeManager::setCustom(const ThemeColors& c, const QString& name)
{
    QSettings s;
    s.setValue(QStringLiteral("theme/custom"), QString::fromUtf8(QJsonDocument(c.toJson()).toJson(QJsonDocument::Compact)));
    s.setValue(QStringLiteral("theme/customName"), name);
}

bool ThemeManager::exportCustom(const ThemeColors& c, const QString& name, const QString& path, QString* error) const
{
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("cruciblecode-theme")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("name"), name},
                           {QStringLiteral("colors"), c.toJson()}};
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

bool ThemeManager::exportCss(const ThemeColors& c, const QString& name, const QString& author, const QString& path, QString* error) const
{
    ThemeCss::Meta m;
    m.name = name;
    m.author = author;
    m.version = QStringLiteral("1.0.0");
    m.description = tr("A custom CrucibleCode theme.");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    f.write(ThemeCss::serialize(c, m).toUtf8());
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

bool ThemeManager::importTheme(const QString& path, ThemeColors* out, QString* name, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    if (QFileInfo(path).suffix().compare(QLatin1String("css"), Qt::CaseInsensitive) == 0) {
        if (f.size() > ThemeCss::kMaxFileBytes) { if (error) *error = tr("The theme file is too large."); return false; }
        const ThemeCss::Result r = ThemeCss::parse(QString::fromUtf8(f.readAll()));
        if (!r.ok) { if (error) *error = r.error; return false; }
        if (out) *out = r.colors;
        if (name) *name = r.meta.name.isEmpty() ? QFileInfo(path).completeBaseName() : r.meta.name;
        return true;
    }
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    if (!d.isObject() || d.object().value(QStringLiteral("format")).toString() != QLatin1String("cruciblecode-theme")) {
        if (error) *error = tr("This is not a CrucibleCode theme file.");
        return false;
    }
    ThemeColors c = makeColors(DarkGrey);
    c.fromJson(d.object().value(QStringLiteral("colors")).toObject());
    if (out) *out = c;
    if (name) *name = d.object().value(QStringLiteral("name")).toString(QStringLiteral("Imported Theme"));
    return true;
}

ThemeManager::Id ThemeManager::nameToId(const QString& name, Id fallback)
{
    const QString n = name.toLower();
    if (n == QLatin1String("light")) return Light;
    if (n == QLatin1String("custom")) return Custom;
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
        c.findMatch = "#6A5322"; c.gitAdded = "#587C0C"; c.gitModified = "#1B81A8"; c.gitDeleted = "#C74E39";
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
        c.findMatch = "#F7DC6F"; c.gitAdded = "#2EA043"; c.gitModified = "#1F6FEB"; c.gitDeleted = "#D1242F";
    }
    return c;
}

void ThemeManager::apply(Id id)
{
    if (id == Named) return;                  // needs a key, see applyKey()
    m_id = id;
    m_key = idToName(id);
    m_name = id == Custom ? customName() : id == Light ? tr("Light") : tr("Dark Grey");
    m_colors = (id == Custom) ? customColors() : makeColors(id);
    applyColors();
}

bool ThemeManager::applyKey(const QString& key)
{
    if (key == QLatin1String("dark")) { apply(DarkGrey); return true; }
    if (key == QLatin1String("light")) { apply(Light); return true; }
    if (key == QLatin1String("custom")) { apply(Custom); return true; }

    ThemeColors c;
    const ThemeInfo info = ThemeLibrary::info(key);
    if (info.isValid() && ThemeLibrary::load(key, &c)) {
        m_id = Named;
        m_key = key;
        m_name = info.name;
        m_colors = c;
        applyColors();
        return true;
    }
    apply(DarkGrey);                          // missing or broken theme: never leave the UI unstyled
    return false;
}

bool ThemeManager::select(const QString& key)
{
    const bool ok = applyKey(key);
    QSettings().setValue(QStringLiteral("theme"), m_key);
    return ok;
}

void ThemeManager::preview(const ThemeColors& c)
{
    m_id = Custom;
    m_key = QStringLiteral("custom");
    m_name = customName();
    m_colors = c;
    applyColors();
}

void ThemeManager::applyColors()
{
    const ThemeColors& c = m_colors;
    if (!m_haveBaseFont) { m_baseFont = qApp->font(); m_haveBaseFont = true; }
    QFont uf = m_baseFont;
    if (c.uiFontSize > 0) uf.setPointSizeF(c.uiFontSize);
    if (qApp->font() != uf) qApp->setFont(uf);

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
QPushButton, QToolButton, QLineEdit, QSpinBox, QComboBox, QTabBar::tab { border-radius: %radius%px; }
QMainWindow, QDialog { background: %window%; color: %text%; }
QWidget { color: %text%; }
QMainWindow::separator { background: %window%; width: 4px; height: 4px; }
QMainWindow::separator:hover { background: %accent%; }
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
    s.replace(QStringLiteral("%radius%"), QString::number(c.radius));
    return s;
}
