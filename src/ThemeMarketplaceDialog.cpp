#include "ThemeMarketplaceDialog.h"
#include "ThemeCss.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTabBar>
#include <QUrl>
#include <QVBoxLayout>

// =====================================================================================================
// ThemePreviewWidget
// =====================================================================================================

ThemePreviewWidget::ThemePreviewWidget(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ThemePreviewWidget::setColors(const ThemeColors& c)
{
    m_colors = c;
    m_has = true;
    update();
}

void ThemePreviewWidget::setMessage(const QString& text)
{
    m_has = false;
    m_message = text;
    update();
}

namespace {
struct Tok {
    QString text;
    QColor color;
    QColor bg;             // invalid = none
};
using Line = QVector<Tok>;
}

void ThemePreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = rect();
    if (!m_has) {
        p.fillRect(r, palette().color(QPalette::Window));
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(r.adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                   m_message.isEmpty() ? tr("No preview available") : m_message);
        return;
    }

    const ThemeColors& c = m_colors;
    QFont ui = font();
    ui.setPixelSize(11);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(12);
    const QFontMetrics fu(ui), fm(mono);
    const int W = r.width(), H = r.height();
    p.fillRect(r, c.window);

    // ---- menu bar
    const int menuH = fu.height() + 8;
    p.fillRect(0, 0, W, menuH, c.panel);
    p.setPen(c.border);
    p.drawLine(0, menuH - 1, W, menuH - 1);
    p.setFont(ui);
    p.setPen(c.text);
    int x = 8;
    for (const QString& m : {tr("File"), tr("Edit"), tr("View"), tr("Build"), tr("Git"), tr("Tools"), tr("Help")}) {
        const int w = fu.horizontalAdvance(m);
        if (x + w > W - 8) break;
        p.drawText(x, 0, w, menuH, Qt::AlignVCenter, m);
        x += w + 14;
    }

    // ---- status bar
    const int statusH = fu.height() + 6;
    const QRect status(0, H - statusH, W, statusH);
    p.fillRect(status, c.accent);
    p.setPen(c.accentText);
    p.drawText(status.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
               fu.elidedText(QStringLiteral("main.cpp     Ln 7, Col 24     UTF-8     C++"), Qt::ElideRight, W - 16));

    // ---- output panel (only when there is room)
    const int lineH = fm.height() + 2;
    const int panelTitleH = fu.height() + 8;
    const int panelH = H >= 270 ? panelTitleH + lineH * 3 + 8 : 0;
    const QRect panel(0, status.top() - panelH, W, panelH);
    if (panelH) {
        p.fillRect(panel, c.base);
        p.setPen(c.border);
        p.drawLine(panel.left(), panel.top(), panel.right(), panel.top());
        p.fillRect(0, panel.top() + 1, W, panelTitleH - 1, c.panel);
        p.setFont(ui);
        const QString t1 = tr("OUTPUT");
        const int w1 = fu.horizontalAdvance(t1);
        p.setPen(c.text);
        p.drawText(10, panel.top() + 1, w1, panelTitleH - 1, Qt::AlignVCenter, t1);
        p.fillRect(10, panel.top() + panelTitleH - 2, w1, 2, c.accent);
        p.setPen(c.textDim);
        p.drawText(10 + w1 + 16, panel.top() + 1, 200, panelTitleH - 1, Qt::AlignVCenter, tr("PROBLEMS"));
        p.setFont(mono);
        const struct { QColor color; QString text; } out[] = {
            {c.error, QStringLiteral("main.cpp:7: error: 'greet' was not declared in this scope")},
            {c.warning, QStringLiteral("main.cpp:5: warning: unused variable 'count'")},
            {c.success, tr("Build finished.")},
        };
        int oy = panel.top() + panelTitleH + 3;
        for (const auto& o : out) {
            p.setPen(o.color);
            p.drawText(QRect(10, oy, W - 20, lineH), Qt::AlignVCenter, fm.elidedText(o.text, Qt::ElideRight, W - 20));
            oy += lineH;
        }
    }

    const QRect body(0, menuH, W, panel.top() - menuH);

    // ---- explorer
    const int exW = qBound(90, W / 4, 160);
    const QRect ex(0, body.top(), exW, body.height());
    p.fillRect(ex, c.window);
    p.setPen(c.border);
    p.drawLine(ex.right(), ex.top(), ex.right(), ex.bottom());
    const int rowH = fu.height() + 6;
    p.setFont(ui);
    p.fillRect(ex.left(), ex.top(), exW - 1, rowH, c.panel);
    p.setPen(c.textDim);
    p.drawText(8, ex.top(), exW - 12, rowH, Qt::AlignVCenter, tr("EXPLORER"));
    struct Item { QString name; QColor color; QString badge; bool selected; int indent; };
    const Item items[] = {
        {QStringLiteral("demo"), c.text, QString(), false, 6},
        {QStringLiteral("main.cpp"), c.gitModified, QStringLiteral("M"), true, 18},
        {QStringLiteral("utils.h"), c.gitAdded, QStringLiteral("A"), false, 18},
        {QStringLiteral("old.cpp"), c.gitDeleted, QStringLiteral("D"), false, 18},
        {QStringLiteral("README.md"), c.text, QString(), false, 18},
        {QStringLiteral("CMakeLists.txt"), c.text, QString(), false, 18},
    };
    int iy = ex.top() + rowH;
    for (const Item& it : items) {
        if (iy + rowH > ex.bottom()) break;
        if (it.selected) p.fillRect(ex.left(), iy, exW - 1, rowH, c.selection);
        p.setPen(it.color);
        p.drawText(it.indent, iy, exW - it.indent - 22, rowH, Qt::AlignVCenter, fu.elidedText(it.name, Qt::ElideRight, exW - it.indent - 22));
        if (!it.badge.isEmpty()) p.drawText(ex.left(), iy, exW - 8, rowH, Qt::AlignVCenter | Qt::AlignRight, it.badge);
        iy += rowH;
    }

    // ---- editor tabs
    const int edX = exW;
    const int tabH = fu.height() + 8;
    p.fillRect(edX, body.top(), W - edX, tabH, c.window);
    int tx = edX;
    const QString names[] = {QStringLiteral("main.cpp"), QStringLiteral("utils.h")};
    for (int i = 0; i < 2; ++i) {
        const int tw = fu.horizontalAdvance(names[i]) + 24;
        if (tx + tw > W) break;
        const QRect tab(tx, body.top(), tw, tabH);
        p.fillRect(tab, i == 0 ? c.base : c.panel);
        p.setPen(c.border);
        p.drawRect(tab.adjusted(0, 0, 0, 0));
        if (i == 0) p.fillRect(tab.left() + 1, tab.top(), tw - 1, 2, c.accent);
        p.setPen(i == 0 ? c.text : c.textDim);
        p.drawText(tab, Qt::AlignCenter, names[i]);
        tx += tw - 1;
    }

    // ---- code
    const QRect code(edX, body.top() + tabH, W - edX, body.height() - tabH);
    p.fillRect(code, c.base);
    const int gutterW = fm.horizontalAdvance(QStringLiteral("00")) + 18;
    p.fillRect(code.left(), code.top(), gutterW, code.height(), c.gutterBg);

    const QColor none;
    const QVector<Line> lines = {
        {{QStringLiteral("#include <iostream>"), c.preproc, none}},
        {},
        {{QStringLiteral("// Forge a greeting"), c.comment, none}},
        {{QStringLiteral("int"), c.keyword, none}, {QStringLiteral(" "), c.text, none}, {QStringLiteral("main"), c.function, none},
         {QStringLiteral("() {"), c.text, none}},
        {{QStringLiteral("    "), c.text, none}, {QStringLiteral("std::string"), c.type, none}, {QStringLiteral(" name = "), c.text, none},
         {QStringLiteral("\"World\""), c.string, none}, {QStringLiteral(";"), c.text, none}},
        {{QStringLiteral("    "), c.text, none}, {QStringLiteral("int"), c.keyword, none}, {QStringLiteral(" "), c.text, none},
         {QStringLiteral("count"), c.text, c.findMatch}, {QStringLiteral(" = "), c.text, none}, {QStringLiteral("42"), c.number, none},
         {QStringLiteral(";"), c.text, none}},
        {{QStringLiteral("    "), c.text, none}, {QStringLiteral("greet"), c.function, none}, {QStringLiteral("("), c.text, none},
         {QStringLiteral("name"), c.text, c.selection}, {QStringLiteral(", "), c.text, none},
         {QStringLiteral("count"), c.text, c.findMatch}, {QStringLiteral(");"), c.text, none}},
        {{QStringLiteral("    "), c.text, none}, {QStringLiteral("return"), c.keyword, none}, {QStringLiteral(" "), c.text, none},
         {QStringLiteral("0"), c.number, none}, {QStringLiteral(";"), c.text, none}},
        {{QStringLiteral("}"), c.text, none}},
    };
    const int current = 6;             // zero-based: line 7
    p.setClipRect(code);
    int y = code.top() + 5;
    for (int i = 0; i < lines.size(); ++i, y += lineH) {
        if (y + lineH > code.bottom()) break;
        if (i == current) p.fillRect(code.left() + gutterW, y, code.width() - gutterW, lineH, c.lineHighlight);
        p.setFont(mono);
        p.setPen(i == current ? c.gutterActive : c.gutterFg);
        p.drawText(code.left(), y, gutterW - 10, lineH, Qt::AlignVCenter | Qt::AlignRight, QString::number(i + 1));
        // Git change markers in the gutter
        if (i == 5) p.fillRect(code.left() + gutterW - 5, y + 1, 3, lineH - 2, c.gitModified);
        if (i == 7) p.fillRect(code.left() + gutterW - 5, y + 1, 3, lineH - 2, c.gitAdded);
        if (i == 8) p.fillRect(code.left() + gutterW - 5, y, 3, 3, c.gitDeleted);

        int cx = code.left() + gutterW + 8;
        for (const Tok& t : lines[i]) {
            const int w = fm.horizontalAdvance(t.text);
            if (t.bg.isValid()) p.fillRect(cx, y, w, lineH, t.bg);
            p.setPen(t.color);
            p.drawText(cx, y + (lineH - fm.height()) / 2 + fm.ascent(), t.text);
            cx += w;
        }
        if (i == current) p.fillRect(cx, y + 1, 2, lineH - 2, c.text);          // caret
    }
    p.setClipping(false);

    // ---- frame
    p.setPen(c.border);
    p.drawRect(r.adjusted(0, 0, -1, -1));
}

// =====================================================================================================
// ThemeMarketplaceDialog
// =====================================================================================================

ThemeMarketplaceDialog::ThemeMarketplaceDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Theme Marketplace"));
    resize(1000, 680);
    setMinimumSize(760, 520);

    m_market = new ThemeMarketplace(this);
    connect(m_market, &ThemeMarketplace::indexLoaded, this, &ThemeMarketplaceDialog::onIndexLoaded);
    connect(m_market, &ThemeMarketplace::indexFailed, this, &ThemeMarketplaceDialog::onIndexFailed);
    connect(m_market, &ThemeMarketplace::themeFetched, this, &ThemeMarketplaceDialog::onThemeFetched);
    connect(m_market, &ThemeMarketplace::themeFailed, this, &ThemeMarketplaceDialog::onThemeFailed);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] {
        updateStatusStyles();
        rebuildList();
    });

    buildUi();

    const bool cached = m_market->loadCachedIndex();
    rebuildList();
    if (cached) startPrefetch();
    loadIndex();
}

void ThemeMarketplaceDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    // search / filter / refresh
    auto* top = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search themes by name, author or tag..."));
    m_search->setClearButtonEnabled(true);
    m_type = new QComboBox;
    m_type->addItem(tr("All themes"), QString());
    m_type->addItem(tr("Dark themes"), QStringLiteral("dark"));
    m_type->addItem(tr("Light themes"), QStringLiteral("light"));
    m_refresh = new QPushButton(tr("Refresh"));
    m_refresh->setToolTip(tr("Download the theme list again"));
    top->addWidget(m_search, 1);
    top->addWidget(m_type);
    top->addWidget(m_refresh);
    root->addLayout(top);

    m_tabs = new QTabBar;
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->addTab(tr("Marketplace"));
    m_tabs->addTab(tr("Installed"));
    m_tabs->addTab(tr("Built-in"));
    root->addWidget(m_tabs);

    // list | details
    auto* split = new QSplitter;
    m_list = new QListWidget;
    m_list->setIconSize(QSize(48, 32));
    m_list->setUniformItemSizes(true);
    m_list->setMinimumWidth(280);
    split->addWidget(m_list);

    auto* detail = new QWidget;
    auto* dl = new QVBoxLayout(detail);
    dl->setContentsMargins(8, 0, 0, 0);
    m_preview = new ThemePreviewWidget;
    dl->addWidget(m_preview, 1);
    m_name = new QLabel;
    QFont nf = font();
    nf.setPointSizeF(nf.pointSizeF() * 1.6);
    nf.setBold(true);
    m_name->setFont(nf);
    m_meta = new QLabel;
    m_state = new QLabel;
    m_description = new QLabel;
    m_description->setWordWrap(true);
    m_credits = new QLabel;
    m_credits->setWordWrap(true);
    dl->addWidget(m_name);
    dl->addWidget(m_meta);
    dl->addWidget(m_state);
    dl->addWidget(m_description);
    dl->addWidget(m_credits);

    auto* btns = new QHBoxLayout;
    m_install = new QPushButton(tr("Install"));
    m_uninstall = new QPushButton(tr("Uninstall"));
    m_apply = new QPushButton(tr("Apply"));
    m_unapply = new QPushButton(tr("Unapply"));
    m_update = new QPushButton(tr("Update"));
    m_install->setToolTip(tr("Download this theme and keep it on this computer"));
    m_uninstall->setToolTip(tr("Remove this theme from this computer"));
    m_apply->setToolTip(tr("Use this theme now (it is installed first if needed)"));
    m_unapply->setToolTip(tr("Stop using this theme and go back to the one you used before"));
    for (QPushButton* b : {m_install, m_uninstall, m_apply, m_unapply, m_update}) btns->addWidget(b);
    btns->addStretch(1);
    dl->addLayout(btns);
    split->addWidget(detail);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({320, 680});
    root->addWidget(split, 1);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    // bottom row
    auto* bottom = new QHBoxLayout;
    auto* fromFile = new QPushButton(tr("Install from File..."));
    fromFile->setToolTip(tr("Add a theme-name.css file from your computer"));
    auto* openDir = new QPushButton(tr("Open Themes Folder"));
    auto* submit = new QPushButton(tr("Submit Your Theme..."));
    submit->setToolTip(tr("Open the CrucibleCode-Themes repository on GitHub, where themes are shared with a pull request"));
    auto* close = new QPushButton(tr("Close"));
    bottom->addWidget(fromFile);
    bottom->addWidget(openDir);
    bottom->addWidget(submit);
    bottom->addStretch(1);
    bottom->addWidget(close);
    root->addLayout(bottom);

    // wiring
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildList(); });
    connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { rebuildList(); });
    connect(m_tabs, &QTabBar::currentChanged, this, [this] { rebuildList(); });
    connect(m_refresh, &QPushButton::clicked, this, [this] { loadIndex(); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { updateDetails(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] {
        if (const Row* r = currentRow(); r && !m_busy && !isApplied(*r)) startAction(Action::Apply);
    });
    connect(m_install, &QPushButton::clicked, this, [this] { startAction(Action::Install); });
    connect(m_apply, &QPushButton::clicked, this, [this] { startAction(Action::Apply); });
    connect(m_update, &QPushButton::clicked, this, [this] { startAction(Action::Update); });
    connect(m_uninstall, &QPushButton::clicked, this, [this] { if (const Row* r = currentRow()) uninstall(Row(*r)); });
    connect(m_unapply, &QPushButton::clicked, this, [this] { if (const Row* r = currentRow()) unapply(Row(*r)); });
    connect(fromFile, &QPushButton::clicked, this, &ThemeMarketplaceDialog::installFromFile);
    connect(openDir, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(ThemeLibrary::themesDir())); });
    connect(submit, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl(ThemeMarketplace::repoUrl())); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    updateStatusStyles();
}

// ---- list ------------------------------------------------------------------------------------------

const ThemeMarketplaceDialog::Row* ThemeMarketplaceDialog::currentRow() const
{
    const int i = m_list ? m_list->currentRow() : -1;
    if (i < 0 || !m_list->currentItem()) return nullptr;
    const int idx = m_list->currentItem()->data(Qt::UserRole).toInt();
    return (idx >= 0 && idx < m_rows.size()) ? &m_rows[idx] : nullptr;
}

bool ThemeMarketplaceDialog::isApplied(const Row& r) const
{
    return (r.builtin || r.installed) && ThemeManager::instance().currentKey() == r.key;
}

QString ThemeMarketplaceDialog::statusFor(const Row& r) const
{
    if (isApplied(r)) return tr("Applied");
    if (r.installed && !r.builtin && !r.local && r.remote && ThemeLibrary::compareVersions(r.remoteVersion, r.installedVersion) > 0)
        return tr("Update available");
    if (r.installed && !r.builtin) return tr("Installed");
    return QString();
}

bool ThemeMarketplaceDialog::colorsFor(const Row& r, ThemeColors* out)
{
    if (r.builtin || r.installed) {
        auto it = m_colorCache.constFind(r.key);
        if (it == m_colorCache.constEnd()) {
            ThemeColors c;
            if (!ThemeLibrary::load(r.key, &c)) return false;
            it = m_colorCache.insert(r.key, c);
        }
        *out = it.value();
        return true;
    }
    const QString ck = remoteKey(r.id, r.remoteVersion);
    auto it = m_remoteColors.constFind(ck);
    if (it == m_remoteColors.constEnd()) {
        const QByteArray css = m_remoteCss.value(ck);
        if (css.isEmpty()) return false;
        const ThemeCss::Result res = ThemeCss::parse(QString::fromUtf8(css));
        if (!res.ok) return false;
        it = m_remoteColors.insert(ck, res.colors);
    }
    *out = it.value();
    return true;
}

// 48x32 colour swatch: window / editor / accent strips and three syntax colours
QIcon ThemeMarketplaceDialog::iconFor(const Row& r)
{
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(int(48 * dpr), int(32 * dpr));
    pm.setDevicePixelRatio(dpr);
    ThemeColors c;
    QPainter p(&pm);
    if (colorsFor(r, &c)) {
        p.fillRect(0, 0, 48, 32, c.base);
        p.fillRect(0, 0, 48, 6, c.window);
        p.fillRect(0, 27, 48, 5, c.accent);
        p.fillRect(5, 10, 14, 3, c.keyword);
        p.fillRect(22, 10, 18, 3, c.string);
        p.fillRect(5, 16, 10, 3, c.function);
        p.fillRect(18, 16, 20, 3, c.comment);
        p.fillRect(5, 22, 22, 2, c.text);
        p.setPen(c.border);
    } else {
        p.fillRect(0, 0, 48, 32, r.type == QLatin1String("light") ? QColor(0xe9, 0xe9, 0xe9) : QColor(0x3a, 0x3a, 0x3a));
        p.setPen(r.type == QLatin1String("light") ? QColor(0x70, 0x70, 0x70) : QColor(0x9a, 0x9a, 0x9a));
        p.drawText(QRect(0, 0, 48, 32), Qt::AlignCenter, QStringLiteral("..."));
        p.setPen(QColor(0x80, 0x80, 0x80));
    }
    p.drawRect(0, 0, 47, 31);
    p.end();
    return QIcon(pm);
}

void ThemeMarketplaceDialog::rebuildList()
{
    if (!m_list) return;
    const Row* cur = currentRow();
    const QString keep = cur ? cur->listKey : QString();

    const QString filter = m_search->text().trimmed();
    const QString typeFilter = m_type->currentData().toString();
    const int tab = qMax(0, m_tabs->currentIndex());

    QVector<Row> all[3];
    const QList<ThemeInfo> installed = ThemeLibrary::installedThemes();
    QHash<QString, ThemeInfo> installedById;
    for (const ThemeInfo& t : installed) installedById.insert(t.id, t);

    for (const MarketTheme& t : m_market->themes()) {
        Row r;
        r.listKey = QStringLiteral("m:") + t.id;
        r.id = t.id;
        r.key = ThemeLibrary::installedKey(t.id);
        r.name = t.name; r.author = t.author; r.version = t.version; r.description = t.description;
        r.type = t.type; r.credits = t.credits; r.tags = t.tags;
        r.remote = true;
        r.remoteVersion = t.version;
        const auto it = installedById.constFind(t.id);
        if (it != installedById.constEnd()) {
            r.installed = true;
            r.installedVersion = it->version;
            r.local = it->local;
        }
        all[MarketplaceTab] << r;
    }
    for (const ThemeInfo& t : installed) {
        Row r;
        r.listKey = QStringLiteral("i:") + t.id;
        r.id = t.id;
        r.key = t.key;
        r.name = t.name; r.author = t.author; r.version = t.version; r.description = t.description;
        r.type = t.type; r.credits = t.credits;
        r.installed = true;
        r.installedVersion = t.version;
        r.local = t.local;
        const MarketTheme m = m_market->find(t.id);
        if (!t.local && !m.id.isEmpty()) {
            r.remote = true;
            r.remoteVersion = m.version;
            r.tags = m.tags;
        }
        all[InstalledTab] << r;
    }
    for (const ThemeInfo& t : ThemeLibrary::builtinThemes()) {
        Row r;
        r.listKey = QStringLiteral("b:") + t.id;
        r.id = t.id;
        r.key = t.key;
        r.name = t.name; r.author = t.author; r.version = t.version; r.description = t.description;
        r.type = t.type; r.credits = t.credits;
        r.builtin = true;
        all[BuiltInTab] << r;
    }
    for (int i = 0; i < 3; ++i) m_total[i] = all[i].size();
    m_tabs->setTabText(0, tr("Marketplace (%1)").arg(m_total[0]));
    m_tabs->setTabText(1, tr("Installed (%1)").arg(m_total[1]));
    m_tabs->setTabText(2, tr("Built-in (%1)").arg(m_total[2]));

    m_rows.clear();
    for (const Row& r : all[tab]) {
        if (!typeFilter.isEmpty() && r.type != typeFilter) continue;
        if (!filter.isEmpty()) {
            const QString hay = r.name + QLatin1Char(' ') + r.author + QLatin1Char(' ') + r.description + QLatin1Char(' ') + r.id
                                + QLatin1Char(' ') + r.tags.join(QLatin1Char(' '));
            if (!hay.contains(filter, Qt::CaseInsensitive)) continue;
        }
        m_rows << r;
    }

    const QSignalBlocker block(m_list);
    m_list->clear();
    int select = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        QStringList sub;
        if (!r.author.isEmpty()) sub << tr("by %1").arg(r.author);
        sub << (r.type == QLatin1String("light") ? tr("Light") : tr("Dark"));
        const QString st = statusFor(r);
        if (!st.isEmpty()) sub << st;
        auto* item = new QListWidgetItem(iconFor(r), r.name + QLatin1Char('\n') + sub.join(QStringLiteral("  \u00b7  ")));
        item->setData(Qt::UserRole, i);
        item->setData(Qt::UserRole + 1, r.id);
        item->setSizeHint(QSize(0, 46));
        m_list->addItem(item);
        if (r.listKey == keep) select = i;
    }
    if (select < 0 && !m_rows.isEmpty()) select = 0;
    if (select >= 0) m_list->setCurrentRow(select);
    updateDetails();
}

// ---- details ---------------------------------------------------------------------------------------

void ThemeMarketplaceDialog::updateStatusStyles()
{
    const ThemeColors& c = ThemeManager::instance().colors();
    const QString dim = QStringLiteral("color: %1;").arg(c.textDim.name());
    m_meta->setStyleSheet(dim);
    m_credits->setStyleSheet(dim);
    m_status->setStyleSheet(dim);
}

void ThemeMarketplaceDialog::updateDetails()
{
    const Row* r = currentRow();
    const bool busy = m_busy;

    // the status line under the list: connection state
    QString status;
    if (m_loadingIndex) status = tr("Loading the theme list...");
    else if (m_offline) status = tr("Could not reach the theme repository. Showing the last downloaded list.");
    else if (m_market->themes().isEmpty() && m_tabs->currentIndex() == MarketplaceTab) status = tr("No community themes found yet.");
    if (!status.isEmpty() || !m_busy) m_status->setText(status);

    if (!r) {
        m_preview->setMessage(m_tabs->currentIndex() == MarketplaceTab && m_loadingIndex ? tr("Loading themes...")
                                                                                         : tr("Select a theme to see a preview."));
        m_name->clear(); m_meta->clear(); m_state->clear(); m_description->clear(); m_credits->clear();
        for (QPushButton* b : {m_install, m_uninstall, m_apply, m_unapply, m_update}) b->setEnabled(false);
        m_update->setVisible(false);
        return;
    }

    m_name->setText(r->name);
    QStringList meta;
    if (!r->author.isEmpty()) meta << tr("by %1").arg(r->author);
    if (!r->version.isEmpty()) meta << tr("version %1").arg(r->version);
    meta << (r->type == QLatin1String("light") ? tr("light theme") : tr("dark theme"));
    if (!r->tags.isEmpty()) meta << r->tags.join(QStringLiteral(", "));
    m_meta->setText(meta.join(QStringLiteral("  \u00b7  ")));
    m_description->setText(r->description);
    m_description->setVisible(!r->description.isEmpty());
    m_credits->setText(r->credits);
    m_credits->setVisible(!r->credits.isEmpty());

    const ThemeColors& tc = ThemeManager::instance().colors();
    const bool applied = isApplied(*r);
    const bool updateAvail = r->installed && !r->builtin && !r->local && r->remote
                             && ThemeLibrary::compareVersions(r->remoteVersion, r->installedVersion) > 0;
    QString state;
    QColor stateColor = tc.textDim;
    if (applied) { state = tr("Applied"); stateColor = tc.success; }
    else if (r->builtin) state = tr("Built-in");
    else if (r->installed) state = r->local ? tr("Installed (from a file)") : tr("Installed");
    else state = tr("Not installed");
    if (updateAvail) state += tr("  -  version %1 is available").arg(r->remoteVersion);
    m_state->setText(state);
    m_state->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(stateColor.name()));

    ThemeColors c;
    if (colorsFor(*r, &c)) {
        m_preview->setColors(c);
    } else {
        const QString ck = remoteKey(r->id, r->remoteVersion);
        if (m_failed.contains(ck)) {
            m_preview->setMessage(tr("The preview could not be loaded:\n%1").arg(m_failed.value(ck)));
        } else {
            m_preview->setMessage(tr("Downloading preview..."));
            ensureRemote(*r);
        }
    }

    m_install->setVisible(!r->builtin);
    m_uninstall->setVisible(!r->builtin);
    m_install->setEnabled(!busy && !r->builtin && !r->installed && r->remote);
    m_uninstall->setEnabled(!busy && !r->builtin && r->installed);
    m_apply->setEnabled(!busy && !applied && (r->builtin || r->installed || r->remote));
    m_unapply->setEnabled(!busy && applied && r->key != QLatin1String("dark"));
    m_update->setVisible(updateAvail);
    m_update->setEnabled(!busy);
    m_update->setText(tr("Update to %1").arg(r->remoteVersion));
}

// ---- marketplace -----------------------------------------------------------------------------------

void ThemeMarketplaceDialog::loadIndex()
{
    m_loadingIndex = true;
    m_offline = false;
    m_refresh->setEnabled(false);
    updateDetails();
    m_market->fetchIndex();
}

void ThemeMarketplaceDialog::onIndexLoaded(int ignored)
{
    m_loadingIndex = false;
    m_offline = false;
    m_refresh->setEnabled(true);
    m_failed.clear();
    rebuildList();
    startPrefetch();
    if (ignored > 0) m_status->setText(tr("%1 invalid entries in the theme index were ignored.").arg(ignored));
}

void ThemeMarketplaceDialog::onIndexFailed(const QString& error)
{
    m_loadingIndex = false;
    m_offline = true;
    m_refresh->setEnabled(true);
    rebuildList();
    m_status->setText(tr("Could not load the theme list: %1").arg(error)
                      + (m_market->themes().isEmpty() ? QString() : tr("\nShowing the last downloaded list.")));
}

void ThemeMarketplaceDialog::ensureRemote(const Row& r)
{
    if (!r.remote || r.installed || r.builtin) return;
    const QString ck = remoteKey(r.id, r.remoteVersion);
    if (m_remoteCss.contains(ck) || m_failed.contains(ck)) return;
    const MarketTheme t = m_market->find(r.id);
    if (t.id.isEmpty()) return;
    const QByteArray cached = m_market->cachedCss(t);
    if (!cached.isEmpty()) {
        m_remoteCss.insert(ck, cached);
        return;
    }
    m_market->fetchTheme(t);
}

void ThemeMarketplaceDialog::startPrefetch()
{
    // fetch the theme files in the background, one at a time, so the list fills with colour swatches
    m_prefetch.clear();
    int count = 0;
    for (const MarketTheme& t : m_market->themes()) {
        const QString ck = remoteKey(t.id, t.version);
        if (m_remoteCss.contains(ck)) continue;
        const QByteArray cached = m_market->cachedCss(t);
        if (!cached.isEmpty()) {
            m_remoteCss.insert(ck, cached);
            continue;
        }
        if (count++ < 60) m_prefetch << t;
    }
    // swatches for what the cache could provide
    for (int i = 0; i < m_list->count(); ++i) {
        const int idx = m_list->item(i)->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_rows.size()) m_list->item(i)->setIcon(iconFor(m_rows[idx]));
    }
    updateDetails();
    pumpPrefetch();
}

void ThemeMarketplaceDialog::pumpPrefetch()
{
    if (!m_prefetchId.isEmpty()) return;
    while (!m_prefetch.isEmpty()) {
        const MarketTheme t = m_prefetch.takeFirst();
        if (m_remoteCss.contains(remoteKey(t.id, t.version))) continue;
        m_prefetchId = t.id;
        m_market->fetchTheme(t);
        return;
    }
}

void ThemeMarketplaceDialog::onThemeFetched(const QString& id, const QByteArray& css)
{
    const MarketTheme t = m_market->find(id);
    if (!t.id.isEmpty()) {
        m_remoteCss.insert(remoteKey(t.id, t.version), css);
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole + 1).toString() != id) continue;
            const int idx = m_list->item(i)->data(Qt::UserRole).toInt();
            if (idx >= 0 && idx < m_rows.size()) m_list->item(i)->setIcon(iconFor(m_rows[idx]));
        }
        const Row* cur = currentRow();
        if (cur && cur->id == id) updateDetails();
    }
    if (id == m_prefetchId) {
        m_prefetchId.clear();
        pumpPrefetch();
    }
    if (m_pending != Action::None && m_pendingId == id) {
        const Action a = m_pending;
        m_pending = Action::None;
        m_pendingId.clear();
        completeAction(a, id);
    }
}

void ThemeMarketplaceDialog::onThemeFailed(const QString& id, const QString& error)
{
    const MarketTheme t = m_market->find(id);
    if (!t.id.isEmpty()) m_failed.insert(remoteKey(t.id, t.version), error);
    if (id == m_prefetchId) {
        m_prefetchId.clear();
        pumpPrefetch();
    }
    if (m_pending != Action::None && m_pendingId == id) {
        m_pending = Action::None;
        m_pendingId.clear();
        setBusy(false);
        QMessageBox::warning(this, tr("Theme Marketplace"), tr("The theme could not be downloaded:\n%1").arg(error));
    }
    const Row* cur = currentRow();
    if (cur && cur->id == id) updateDetails();
}

// ---- actions ---------------------------------------------------------------------------------------

void ThemeMarketplaceDialog::setBusy(bool busy, const QString& text)
{
    m_busy = busy;
    if (busy) m_status->setText(text);
    else m_status->clear();
    updateDetails();
}

void ThemeMarketplaceDialog::startAction(Action a)
{
    const Row* cur = currentRow();
    if (!cur || m_busy) return;
    const Row r = *cur;

    // built-in and installed themes need no download
    if (a == Action::Apply && (r.builtin || r.installed)) {
        applyKey(r.key);
        return;
    }
    if (!r.remote) return;

    const MarketTheme t = m_market->find(r.id);
    if (t.id.isEmpty()) return;
    const QString ck = remoteKey(t.id, t.version);
    if (!m_remoteCss.contains(ck)) {
        const QByteArray cached = m_market->cachedCss(t);
        if (!cached.isEmpty()) m_remoteCss.insert(ck, cached);
    }
    if (m_remoteCss.contains(ck)) {
        completeAction(a, t.id);
        return;
    }
    m_failed.remove(ck);
    m_pending = a;
    m_pendingId = t.id;
    setBusy(true, tr("Downloading \"%1\"...").arg(t.name));
    m_market->fetchTheme(t);       // finishes in onThemeFetched / onThemeFailed (joins a background download if one is running)
}

void ThemeMarketplaceDialog::completeAction(Action a, const QString& id)
{
    const MarketTheme t = m_market->find(id);
    const QByteArray css = m_remoteCss.value(remoteKey(t.id, t.version));
    if (t.id.isEmpty() || css.isEmpty()) {
        setBusy(false);
        return;
    }
    const bool wasApplied = ThemeManager::instance().currentKey() == ThemeLibrary::installedKey(t.id);

    ThemeInfo meta;
    meta.id = t.id;
    meta.name = t.name;
    meta.author = t.author;
    meta.version = t.version;
    meta.description = t.description;
    meta.credits = t.credits;
    meta.type = t.type;
    QString err;
    if (!ThemeLibrary::install(meta, css, &err)) {
        setBusy(false);
        QMessageBox::warning(this, tr("Theme Marketplace"), tr("The theme could not be installed:\n%1").arg(err));
        return;
    }
    m_colorCache.clear();
    setBusy(false);

    if (a == Action::Apply || (a == Action::Update && wasApplied)) applyKey(ThemeLibrary::installedKey(t.id));
    else rebuildList();
    m_status->setText(a == Action::Update ? tr("\"%1\" was updated to version %2.").arg(t.name, t.version)
                                           : tr("\"%1\" was installed.").arg(t.name));
}

void ThemeMarketplaceDialog::applyKey(const QString& key)
{
    ThemeManager& tm = ThemeManager::instance();
    const QString previous = tm.currentKey();
    // remember what to go back to when the theme is unapplied
    if (previous != key) QSettings().setValue(QStringLiteral("theme/previous"), previous);
    if (!tm.select(key)) {
        QMessageBox::warning(this, tr("Theme Marketplace"), tr("The theme could not be loaded. Dark Grey is used instead."));
    } else {
        // the theme is re-read from disk, so an updated file shows its new colours
        m_colorCache.remove(key);
    }
    rebuildList();
}

void ThemeMarketplaceDialog::unapply(const Row& r)
{
    ThemeManager& tm = ThemeManager::instance();
    if (tm.currentKey() != r.key) return;
    QString back = QSettings().value(QStringLiteral("theme/previous"), QStringLiteral("dark")).toString();
    if (back == r.key || !ThemeLibrary::exists(back)) back = QStringLiteral("dark");
    tm.select(back);
    rebuildList();
    m_status->setText(tr("\"%1\" was unapplied. Now using %2.").arg(r.name, tm.currentName()));
}

void ThemeMarketplaceDialog::uninstall(const Row& r)
{
    if (!r.installed || r.builtin) return;
    if (QMessageBox::question(this, tr("Uninstall Theme"), tr("Uninstall \"%1\"?").arg(r.name)) != QMessageBox::Yes) return;

    ThemeManager& tm = ThemeManager::instance();
    const bool wasApplied = tm.currentKey() == r.key;
    if (wasApplied) {
        QString back = QSettings().value(QStringLiteral("theme/previous"), QStringLiteral("dark")).toString();
        if (back == r.key || !ThemeLibrary::exists(back)) back = QStringLiteral("dark");
        tm.select(back);                          // never delete the theme that is in use
    }
    QString err;
    if (!ThemeLibrary::uninstall(r.id, &err)) {
        QMessageBox::warning(this, tr("Uninstall Theme"), err);
    }
    m_colorCache.clear();
    rebuildList();
    m_status->setText(tr("\"%1\" was uninstalled.").arg(r.name));
}

void ThemeMarketplaceDialog::installFromFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Install Theme from File"), QString(),
                                                      tr("CrucibleCode theme (*.css)"));
    if (path.isEmpty()) return;
    ThemeInfo info;
    QString err;
    if (!ThemeLibrary::installFromFile(path, &info, &err)) {
        QMessageBox::warning(this, tr("Install Theme from File"), err);
        return;
    }
    m_colorCache.clear();
    m_tabs->setCurrentIndex(InstalledTab);
    m_search->clear();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole + 1).toString() == info.id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
    rebuildList();
    m_status->setText(tr("\"%1\" was installed.").arg(info.name));
}
