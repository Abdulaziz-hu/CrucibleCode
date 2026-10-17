#include "SettingsDialog.h"
#include "AiCompletion.h"
#include "AiSetupDialog.h"
#include "LspManager.h"
#include "ProjectTemplates.h"
#include "ShortcutManager.h"
#include "SnippetDialog.h"
#include "TemplateDialogs.h"
#include "ThemeEditorDialog.h"
#include "ThemeLibrary.h"
#include "ThemeManager.h"
#include "ThemeMarketplaceDialog.h"
#include "Updater.h"
#include <QCoreApplication>
#include <QHeaderView>
#include <QTabWidget>
#include <QTableWidget>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

QFont SettingsDialog::editorFont()
{
    QSettings s;
    QString family = s.value(QStringLiteral("editor/fontFamily")).toString();
    if (family.isEmpty()) {
        const QStringList candidates = {"Cascadia Mono", "Consolas", "JetBrains Mono", "Menlo",
                                        "DejaVu Sans Mono", "Liberation Mono"};
        const QStringList installed = QFontDatabase::families();
        for (const QString& c : candidates) {
            if (installed.contains(c)) { family = c; break; }
        }
    }
    QFont f = family.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(family);
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(s.value(QStringLiteral("editor/fontSize"), 10).toInt());
    return f;
}

bool SettingsDialog::showHiddenFiles()
{
    return QSettings().value(QStringLiteral("explorer/showHidden"), true).toBool();
}

int SettingsDialog::tabSize()
{
    return qBound(1, QSettings().value(QStringLiteral("editor/tabSize"), 4).toInt(), 16);
}

bool SettingsDialog::suggestionsEnabled()
{
    return QSettings().value(QStringLiteral("editor/suggestions"), false).toBool();
}

int SettingsDialog::caretBlinkMode()
{
    return qBound(0, QSettings().value(QStringLiteral("editor/caretBlink"), 0).toInt(), 2);
}

int SettingsDialog::livePort()
{
    return qBound(1024, QSettings().value(QStringLiteral("web/livePort"), 5500).toInt(), 65000);
}

bool SettingsDialog::liveOpenBrowser()
{
    return QSettings().value(QStringLiteral("web/openBrowser"), true).toBool();
}

bool SettingsDialog::gitDecorations() { return QSettings().value(QStringLiteral("explorer/gitStatus"), true).toBool(); }
bool SettingsDialog::gpuEnabled() { return QSettings().value(QStringLiteral("gpu/enabled"), false).toBool(); }
bool SettingsDialog::perfMonitorEnabled() { return QSettings().value(QStringLiteral("perf/enabled"), false).toBool(); }
bool SettingsDialog::trimTrailingWhitespace() { return QSettings().value(QStringLiteral("editor/trimTrailing"), false).toBool(); }
bool SettingsDialog::ensureFinalNewline() { return QSettings().value(QStringLiteral("editor/finalNewline"), false).toBool(); }

bool SettingsDialog::spellCheckEnabled()
{
    return QSettings().value(QStringLiteral("editor/spellcheck"), true).toBool();
}

static QWidget* pageFor(QFormLayout* form)
{
    auto* w = new QWidget;
    w->setLayout(form);
    return w;
}

SettingsDialog::SettingsDialog(AiCompletion* ai, QWidget* parent) : QDialog(parent), m_aiEngine(ai)
{
    setWindowTitle(tr("Settings"));
    setMinimumSize(720, 520);
    QSettings s;
    auto* tabs = new QTabWidget;

    // ---------------- General ----------------
    auto* general = new QFormLayout;
    general->setLabelAlignment(Qt::AlignRight);
    m_originalTheme = ThemeManager::instance().currentKey();
    m_theme = new QComboBox;
    m_theme->setMaxVisibleItems(24);
    fillThemeCombo(m_originalTheme);
    auto* editTheme = new QPushButton(tr("Customize..."));
    editTheme->setToolTip(tr("Edit every colour, import or export the custom theme"));
    auto* market = new QPushButton(tr("Theme Marketplace..."));
    market->setToolTip(tr("Browse, install and apply community themes"));
    auto* themeRow = new QHBoxLayout;
    themeRow->addWidget(m_theme, 1);
    themeRow->addWidget(editTheme);
    themeRow->addWidget(market);
    general->addRow(tr("Theme:"), themeRow);
    connect(editTheme, &QPushButton::clicked, this, &SettingsDialog::editCustomTheme);
    connect(market, &QPushButton::clicked, this, &SettingsDialog::openMarketplace);

    m_hidden = new QCheckBox(tr("Show hidden files in the Explorer (.gitignore, .env, ...)"));
    m_hidden->setChecked(showHiddenFiles());
    general->addRow(tr("Explorer:"), m_hidden);

    m_gitDeco = new QCheckBox(tr("Show Git status (M, U, A, D) and colours next to files in the Explorer and on tabs"));
    m_gitDeco->setChecked(gitDecorations());
    general->addRow(QString(), m_gitDeco);

    m_perf = new QCheckBox(tr("Show CPU, memory and thread usage in the status bar"));
    m_perf->setChecked(perfMonitorEnabled());
    general->addRow(tr("Performance monitor:"), m_perf);

    m_gpu = new QCheckBox(tr("GPU window compositing (restart required; uses roughly 100 MB more memory, rarely noticeable in an editor)"));
    m_gpu->setChecked(gpuEnabled());
    general->addRow(tr("Graphics:"), m_gpu);

    m_trim = new QCheckBox(tr("Trim trailing whitespace when saving"));
    m_trim->setChecked(trimTrailingWhitespace());
    general->addRow(tr("Saving:"), m_trim);
    m_finalNewline = new QCheckBox(tr("Make sure files end with a newline"));
    m_finalNewline->setChecked(ensureFinalNewline());
    general->addRow(QString(), m_finalNewline);
    tabs->addTab(pageFor(general), tr("General"));

    // ---------------- Editor ----------------
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    m_font = new QFontComboBox;
    m_font->setFontFilters(QFontComboBox::MonospacedFonts);
    m_font->setCurrentFont(editorFont());
    form->addRow(tr("Editor font:"), m_font);

    m_fontSize = new QSpinBox;
    m_fontSize->setRange(6, 40);
    m_fontSize->setValue(editorFont().pointSize());
    form->addRow(tr("Font size:"), m_fontSize);
    auto* zoomNote = new QLabel(tr("Zoom (Ctrl +/-, Ctrl+wheel) applies to all files and is remembered."));
    zoomNote->setWordWrap(true);
    form->addRow(QString(), zoomNote);

    m_tabSize = new QSpinBox;
    m_tabSize->setRange(1, 16);
    m_tabSize->setValue(tabSize());
    form->addRow(tr("Tab size (spaces):"), m_tabSize);

    m_caret = new QComboBox;
    m_caret->addItem(tr("Solid while typing, blinks when idle"), 0);
    m_caret->addItem(tr("Always blink"), 1);
    m_caret->addItem(tr("Never blink"), 2);
    m_caret->setCurrentIndex(caretBlinkMode());
    form->addRow(tr("Cursor:"), m_caret);

    m_suggest = new QCheckBox(tr("Code suggestions while typing (keywords and words from the file; Ctrl+Space)"));
    m_suggest->setChecked(suggestionsEnabled());
    form->addRow(tr("Suggestions:"), m_suggest);

    m_spell = new QCheckBox(tr("Spell check Markdown and text files (red underline, right-click for corrections)"));
    m_spell->setChecked(spellCheckEnabled());
    form->addRow(tr("Spelling:"), m_spell);

    m_aiBox = new QCheckBox(tr("AI tab completion with a local model (grey suggestion, Tab to accept)"));
    m_aiBox->setChecked(AiCompletion::enabledInSettings());
    auto* aiSetup = new QPushButton(tr("Set up AI model..."));
    auto* aiRow = new QHBoxLayout;
    aiRow->addWidget(m_aiBox, 1);
    aiRow->addWidget(aiSetup);
    form->addRow(tr("AI:"), aiRow);
    connect(aiSetup, &QPushButton::clicked, this, [this] {
        AiSetupDialog dlg(m_aiEngine, this);
        dlg.exec();
        m_aiBox->setChecked(AiCompletion::enabledInSettings());
    });
    connect(m_aiBox, &QCheckBox::toggled, this, [this](bool on) {
        if (!on || (m_aiEngine->runtimeAvailable() && m_aiEngine->modelAvailable())) return;
        // first use: the local model has to be downloaded
        QSignalBlocker block(m_aiBox);
        m_aiBox->setChecked(false);
        AiSetupDialog dlg(m_aiEngine, this);
        dlg.exec();
        m_aiBox->setChecked(AiCompletion::enabledInSettings());
    });
    tabs->addTab(pageFor(form), tr("Editor"));

    // ---------------- Shortcuts ----------------
    m_shortcuts = new ShortcutsEditor;
    tabs->addTab(m_shortcuts, tr("Shortcuts"));

    // ---------------- Snippets & templates ----------------
    auto* st = new QFormLayout;
    st->setLabelAlignment(Qt::AlignRight);
    auto* snipBtn = new QPushButton(tr("Manage Snippets..."));
    st->addRow(tr("Code snippets:"), snipBtn);
    connect(snipBtn, &QPushButton::clicked, this, [this] { SnippetDialog(this).exec(); });

    m_defaultTemplate = new QComboBox;
    const auto fillTemplates = [this] {
        m_defaultTemplate->clear();
        for (const ProjectTemplate& t : ProjectTemplates::all()) m_defaultTemplate->addItem(t.name, t.id);
        m_defaultTemplate->setCurrentIndex(qMax(0, m_defaultTemplate->findData(ProjectTemplates::defaultId())));
    };
    fillTemplates();
    st->addRow(tr("Default project template:"), m_defaultTemplate);
    m_alwaysDefault = new QCheckBox(tr("Always use the default template for new projects (do not ask)"));
    m_alwaysDefault->setChecked(ProjectTemplates::alwaysUseDefault());
    st->addRow(QString(), m_alwaysDefault);
    m_author = new QLineEdit(ProjectTemplates::author());
    st->addRow(tr("Author ({{author}}):"), m_author);
    auto* tmplBtn = new QPushButton(tr("Manage Project Templates..."));
    st->addRow(QString(), tmplBtn);
    connect(tmplBtn, &QPushButton::clicked, this, [this, fillTemplates] {
        TemplateManagerDialog(QString(), this).exec();
        fillTemplates();
        m_alwaysDefault->setChecked(ProjectTemplates::alwaysUseDefault());
    });
    tabs->addTab(pageFor(st), tr("Snippets && Templates"));

    // ---------------- Language servers ----------------
    auto* lspPage = new QWidget;
    auto* lspLay = new QVBoxLayout(lspPage);
    m_lspEnabled = new QCheckBox(tr("Enable Language Server Protocol (diagnostics, completion with Ctrl+Space, F12 go to definition)"));
    m_lspEnabled->setChecked(LspManager::globalEnabled());
    lspLay->addWidget(m_lspEnabled);
    m_lspTable = new QTableWidget;
    m_lspTable->setColumnCount(5);
    m_lspTable->setHorizontalHeaderLabels({tr("On"), tr("Server"), tr("File extensions"), tr("Command"), tr("Arguments")});
    m_lspTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_lspTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_lspTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_lspTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_lspTable->verticalHeader()->hide();
    fillLspTable(LspManager::loadServers());
    lspLay->addWidget(m_lspTable, 1);
    auto* lspBtns = new QHBoxLayout;
    auto* addSrv = new QPushButton(tr("Add"));
    auto* delSrv = new QPushButton(tr("Remove"));
    auto* defSrv = new QPushButton(tr("Restore Defaults"));
    lspBtns->addWidget(addSrv);
    lspBtns->addWidget(delSrv);
    lspBtns->addStretch(1);
    lspBtns->addWidget(defSrv);
    lspLay->addLayout(lspBtns);
    auto* lspHelp = new QLabel(tr("Language servers are separate programs that must be installed (for example <tt>clangd</tt> for C/C++). "
                                  "Rows shown in red are not found on your PATH. Changes apply after pressing OK."));
    lspHelp->setWordWrap(true);
    lspLay->addWidget(lspHelp);
    connect(addSrv, &QPushButton::clicked, this, [this] {
        LspServerConfig c;
        c.name = tr("New server");
        QList<LspServerConfig> l = LspManager::loadServers();   // keep the table's current content
        l.clear();
        for (int r = 0; r < m_lspTable->rowCount(); ++r) {
            LspServerConfig x;
            x.enabled = m_lspTable->item(r, 0)->checkState() == Qt::Checked;
            x.name = m_lspTable->item(r, 1)->text();
            x.extensions = m_lspTable->item(r, 2)->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
            x.command = m_lspTable->item(r, 3)->text();
            x.args = m_lspTable->item(r, 4)->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            l << x;
        }
        l << c;
        fillLspTable(l);
    });
    connect(delSrv, &QPushButton::clicked, this, [this] {
        const int r = m_lspTable->currentRow();
        if (r >= 0) m_lspTable->removeRow(r);
    });
    connect(defSrv, &QPushButton::clicked, this, [this] { fillLspTable(LspManager::defaultServers()); });
    tabs->addTab(lspPage, tr("Language Servers"));

    // ---------------- Build & web ----------------
    auto* bw = new QFormLayout;
    bw->setLabelAlignment(Qt::AlignRight);
    m_cmake = new QLineEdit(s.value(QStringLiteral("cmake/path")).toString());
    m_cmake->setPlaceholderText(tr("cmake (found on PATH)"));
    bw->addRow(tr("CMake executable:"), m_cmake);
    m_generator = new QLineEdit(s.value(QStringLiteral("cmake/generator")).toString());
    m_generator->setPlaceholderText(tr("Default (e.g. Ninja)"));
    bw->addRow(tr("CMake generator:"), m_generator);
    m_extra = new QLineEdit(s.value(QStringLiteral("cmake/extraArgs")).toString());
    m_extra->setPlaceholderText(tr("-DCMAKE_PREFIX_PATH=/path/to/Qt ..."));
    bw->addRow(tr("Extra configure args:"), m_extra);
    m_livePort = new QSpinBox;
    m_livePort->setRange(1024, 65000);
    m_livePort->setValue(livePort());
    bw->addRow(tr("Live preview port:"), m_livePort);
    m_liveOpen = new QCheckBox(tr("Open the browser when the live preview starts (needs Python 3)"));
    m_liveOpen->setChecked(liveOpenBrowser());
    bw->addRow(QString(), m_liveOpen);
    tabs->addTab(pageFor(bw), tr("Build && Web"));

    // ---------------- Updates ----------------
    auto* up = new QFormLayout;
    up->setLabelAlignment(Qt::AlignRight);
    up->addRow(tr("Installed version:"), new QLabel(QCoreApplication::applicationVersion()));
    up->addRow(tr("Installation type:"), new QLabel(Updater::installTypeName(Updater::installType())));
    m_autoUpdate = new QCheckBox(tr("Check for updates automatically when CrucibleCode starts"));
    m_autoUpdate->setChecked(Updater::autoCheckEnabled());
    up->addRow(tr("Updates:"), m_autoUpdate);
    m_prerelease = new QCheckBox(tr("Include pre-releases"));
    m_prerelease->setChecked(Updater::includePrereleases());
    up->addRow(QString(), m_prerelease);
    auto* checkNow = new QPushButton(tr("Check for Updates Now"));
    up->addRow(QString(), checkNow);
    connect(checkNow, &QPushButton::clicked, this, [this] {
        Updater::setIncludePrereleases(m_prerelease->isChecked());
        emit checkForUpdatesRequested();
    });
    tabs->addTab(pageFor(up), tr("Updates"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* root = new QVBoxLayout(this);
    root->addWidget(tabs);
    root->addWidget(buttons);
}

void SettingsDialog::fillLspTable(const QList<LspServerConfig>& list)
{
    m_lspTable->setRowCount(0);
    for (const LspServerConfig& c : list) {
        const int r = m_lspTable->rowCount();
        m_lspTable->insertRow(r);
        auto* on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        on->setCheckState(c.enabled ? Qt::Checked : Qt::Unchecked);
        m_lspTable->setItem(r, 0, on);
        m_lspTable->setItem(r, 1, new QTableWidgetItem(c.name));
        m_lspTable->setItem(r, 2, new QTableWidgetItem(c.extensions.join(QLatin1Char(','))));
        auto* cmd = new QTableWidgetItem(c.command);
        if (!LspManager::commandAvailable(c.command)) {
            cmd->setForeground(ThemeManager::instance().colors().error);
            cmd->setToolTip(tr("Not found on PATH"));
        }
        m_lspTable->setItem(r, 3, cmd);
        m_lspTable->setItem(r, 4, new QTableWidgetItem(c.args.join(QLatin1Char(' '))));
    }
}

// Native themes, then the built-in CSS themes, then the installed ones. Section headers are not selectable.
void SettingsDialog::fillThemeCombo(const QString& select)
{
    QSignalBlocker block(m_theme);
    m_theme->clear();
    const auto header = [this](const QString& text) {
        m_theme->addItem(text);
        if (auto* model = qobject_cast<QStandardItemModel*>(m_theme->model()))
            model->item(m_theme->count() - 1)->setFlags(Qt::NoItemFlags);
    };
    m_theme->addItem(tr("Dark Grey"), QStringLiteral("dark"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->addItem(tr("Custom Theme"), QStringLiteral("custom"));
    header(tr("--- Built-in themes ---"));
    for (const ThemeInfo& t : ThemeLibrary::builtinThemes())
        if (t.key != QLatin1String("dark") && t.key != QLatin1String("light"))
            m_theme->addItem(t.name, t.key);
    const QList<ThemeInfo> installed = ThemeLibrary::installedThemes();
    if (!installed.isEmpty()) {
        header(tr("--- Installed themes ---"));
        for (const ThemeInfo& t : installed) m_theme->addItem(t.name, t.key);
    }
    const int i = m_theme->findData(select);
    m_theme->setCurrentIndex(i >= 0 ? i : 0);
}

void SettingsDialog::openMarketplace()
{
    ThemeMarketplaceDialog dlg(this);
    dlg.exec();                                    // applies themes immediately, like the Customize dialog
    m_originalTheme = ThemeManager::instance().currentKey();
    fillThemeCombo(m_originalTheme);
}

void SettingsDialog::editCustomTheme()
{
    ThemeEditorDialog dlg(this);
    dlg.exec();                                    // applied and saved by the dialog on OK, restored on Cancel
    m_originalTheme = ThemeManager::instance().currentKey();
    fillThemeCombo(m_originalTheme);
}

void SettingsDialog::accept()
{
    QSettings s;
    s.setValue(QStringLiteral("editor/fontFamily"), m_font->currentFont().family());
    s.setValue(QStringLiteral("editor/fontSize"), m_fontSize->value());
    s.setValue(QStringLiteral("editor/tabSize"), m_tabSize->value());
    s.setValue(QStringLiteral("explorer/showHidden"), m_hidden->isChecked());
    s.setValue(QStringLiteral("editor/caretBlink"), m_caret->currentData().toInt());
    s.setValue(QStringLiteral("web/livePort"), m_livePort->value());
    s.setValue(QStringLiteral("web/openBrowser"), m_liveOpen->isChecked());
    s.setValue(QStringLiteral("editor/suggestions"), m_suggest->isChecked());
    s.setValue(QStringLiteral("editor/spellcheck"), m_spell->isChecked());
    s.setValue(QStringLiteral("editor/trimTrailing"), m_trim->isChecked());
    s.setValue(QStringLiteral("editor/finalNewline"), m_finalNewline->isChecked());
    s.setValue(QStringLiteral("perf/enabled"), m_perf->isChecked());
    s.setValue(QStringLiteral("gpu/enabled"), m_gpu->isChecked());
    s.setValue(QStringLiteral("explorer/gitStatus"), m_gitDeco->isChecked());
    s.setValue(QStringLiteral("ai/enabled"), m_aiBox->isChecked());
    s.setValue(QStringLiteral("cmake/path"), m_cmake->text().trimmed());
    s.setValue(QStringLiteral("cmake/generator"), m_generator->text().trimmed());
    s.setValue(QStringLiteral("cmake/extraArgs"), m_extra->text());

    m_shortcuts->apply();

    ProjectTemplates::setDefaultId(m_defaultTemplate->currentData().toString());
    ProjectTemplates::setAlwaysUseDefault(m_alwaysDefault->isChecked());
    ProjectTemplates::setAuthor(m_author->text().trimmed());

    LspManager::setGlobalEnabled(m_lspEnabled->isChecked());
    QList<LspServerConfig> servers;
    for (int r = 0; r < m_lspTable->rowCount(); ++r) {
        LspServerConfig c;
        c.enabled = m_lspTable->item(r, 0)->checkState() == Qt::Checked;
        c.name = m_lspTable->item(r, 1)->text().trimmed();
        for (const QString& e : m_lspTable->item(r, 2)->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) c.extensions << e.trimmed().remove(QLatin1Char('.'));
        c.command = m_lspTable->item(r, 3)->text().trimmed();
        c.args = m_lspTable->item(r, 4)->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (!c.name.isEmpty() && !c.command.isEmpty()) servers << c;
    }
    LspManager::saveServers(servers);

    Updater::setAutoCheckEnabled(m_autoUpdate->isChecked());
    Updater::setIncludePrereleases(m_prerelease->isChecked());

    const QString theme = m_theme->currentData().toString();
    if (!theme.isEmpty() && theme != ThemeManager::instance().currentKey())
        ThemeManager::instance().select(theme);
    QDialog::accept();
}
