#include "MainWindow.h"
#include "CodeEditor.h"
#include "AiCompletion.h"
#include "AppInfo.h"
#include "FileViewers.h"
#include "GitManager.h"
#include "GitPanel.h"
#include "FindBar.h"
#include "LiveServer.h"
#include "LspManager.h"
#include "PerfMonitor.h"
#include "ProjectTemplates.h"
#include "ShortcutManager.h"
#include "SnippetDialog.h"
#include "Snippets.h"
#include "TemplateDialogs.h"
#include "ThemeEditorDialog.h"
#include "UpdateDialog.h"
#include "Updater.h"
#include "QuickOpenDialog.h"
#include "RunHelper.h"
#include "SearchPanel.h"
#include "SettingsDialog.h"
#include "SyntaxHighlighter.h"
#include "TerminalPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QTextBlock>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QPixmap>
#include <QPointer>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kMaxRecent = 8;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("CrucibleCode"));
    setAcceptDrops(true);
    resize(1280, 800);
    setDockOptions(AllowTabbedDocks | AnimatedDocks | AllowNestedDocks);
    setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::South);

    m_build = new BuildManager(this);
    m_ai = new AiCompletion(this);
    m_lsp = new LspManager(this);
    m_perf = new PerfMonitor(this);
    m_updater = new Updater(this);
    m_git = new GitManager(this);
    connect(m_git, &GitManager::statusChanged, this, &MainWindow::updateGitUi);
    connect(m_git, &GitManager::output, this, [this](const QString& t) { appendOutput(QStringLiteral("[git] ") + t, BuildManager::Info); });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState st) {
        if (st == Qt::ApplicationActive) m_git->requestRefresh(300);
    });
    m_live = new LiveServer(this);
    connect(m_live, &LiveServer::stateChanged, this, &MainWindow::updateLiveUi);
    connect(m_live, &LiveServer::message, this, [this](const QString& t) { appendOutput(QStringLiteral("[live] ") + t, BuildManager::Info); });
    connect(m_live, &LiveServer::ready, this, [this](const QUrl&) {
        if (SettingsDialog::liveOpenBrowser()) openLiveInBrowser();
    });
    connect(m_ai, &AiCompletion::stateChanged, this, &MainWindow::updateAiLabel);
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onExternalChange);

    // Central area: editor tabs + find bar
    auto* central = new QWidget;
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_findBar = new FindBar;

    // "Preview" button in the top-right corner of the tab bar, visible for Markdown files
    m_mdButton = new QToolButton;
    m_mdButton->setText(tr("Preview"));
    m_mdButton->setToolTip(tr("Markdown preview (Ctrl+Shift+V)"));
    m_mdButton->setCheckable(true);
    m_mdButton->hide();
    connect(m_mdButton, &QToolButton::clicked, this, [this] { m_mdDock->setVisible(!m_mdDock->isVisible()); });

    // "Go Live": local Python server with live reload for HTML / CSS / JS
    m_liveButton = new QToolButton;
    m_liveButton->setText(tr("Go Live"));
    m_liveButton->setToolTip(tr("Start / stop the live preview server (Ctrl+Alt+L)"));
    m_liveButton->setCheckable(true);
    m_liveButton->hide();
    m_liveOpenButton = new QToolButton;
    m_liveOpenButton->setText(tr("Open in Browser"));
    m_liveOpenButton->setToolTip(tr("Open the live preview in your browser"));
    m_liveOpenButton->hide();
    connect(m_liveButton, &QToolButton::clicked, this, [this] { toggleLive(); });
    connect(m_liveOpenButton, &QToolButton::clicked, this, [this] { openLiveInBrowser(); });

    auto* corner = new QWidget;
    auto* cornerLay = new QHBoxLayout(corner);
    cornerLay->setContentsMargins(0, 0, 0, 0);
    cornerLay->setSpacing(2);
    cornerLay->addWidget(m_liveOpenButton);
    cornerLay->addWidget(m_liveButton);
    cornerLay->addWidget(m_mdButton);
    m_tabs->setCornerWidget(corner, Qt::TopRightCorner);
    lay->addWidget(m_tabs, 1);
    lay->addWidget(m_findBar);
    setCentralWidget(central);

    createActions();
    createMenus();
    createToolBar();
    createDocks();
    createStatusBar();

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QWidget::customContextMenuRequested, this, &MainWindow::showTabContextMenu);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        m_findBar->setEditor(currentEditor());
        updateStatus();
        updateWindowTitle();
        updateMarkdownButton();
        updateMarkdownPreview();
    });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (CodeEditor* ed = editorAt(i)) ed->applyTheme();
            else if (auto* dv = qobject_cast<DiffViewer*>(m_tabs->widget(i)))
                for (auto* h : dv->document()->findChildren<QSyntaxHighlighter*>()) h->rehighlight();
        if (m_themeGroup) {
            const auto id = ThemeManager::instance().current();
            (id == ThemeManager::Custom ? m_actThemeCustom : ThemeManager::instance().isDark() ? m_actThemeDark : m_actThemeLight)->setChecked(true);
        }
        m_terminals->applyTheme();
        // Re-render Error List coloring
        for (int i = 0; i < m_errors->topLevelItemCount(); ++i) {
            auto* it = m_errors->topLevelItem(i);
            const bool isErr = it->text(0) == tr("Error");
            it->setForeground(0, isErr ? ThemeManager::instance().colors().error
                                       : ThemeManager::instance().colors().warning);
        }
    });

    connect(m_build, &BuildManager::started, this, &MainWindow::onBuildStarted);
    connect(m_build, &BuildManager::message, this, &MainWindow::appendOutput);
    connect(m_build, &BuildManager::diagnostic, this, &MainWindow::addDiagnostic);
    connect(m_build, &BuildManager::finished, this, &MainWindow::onBuildFinished);

    QSettings s;
    m_configCombo->setCurrentText(s.value(QStringLiteral("cmake/config"), QStringLiteral("Debug")).toString());
    m_actWordWrap->setChecked(s.value(QStringLiteral("editor/wordWrap"), false).toBool());
    registerShortcuts();
    connect(m_perf, &PerfMonitor::updated, this, [this] {
        m_perfLabel->setText(m_perf->summary());
        m_perfLabel->setToolTip(m_perf->details());
    });
    connect(m_lsp, &LspManager::statusChanged, this, &MainWindow::updateLspUi);
    connect(m_lsp, &LspManager::problemsChanged, this, &MainWindow::refreshProblems);
    connect(m_lsp, &LspManager::message, this, [this](const QString& t) { statusBar()->showMessage(t, 6000); });
    connect(m_lsp, &LspManager::locationRequested, this, [this](const QString& p, int l, int col) { openFile(p, l, col); });
    applyEditorSettings();
    if (Updater::autoCheckEnabled()) {
        const QDateTime last = QSettings().value(QStringLiteral("update/lastCheck")).toDateTime();
        if (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) > 3600)
            QTimer::singleShot(5000, this, [this] { checkForUpdates(false); });
    }
    restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
    updateRecentMenu();
    updateBuildActions();
    updateStatus();
}

// ---------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------

void MainWindow::createActions()
{
    const auto act = [this](const QString& text, auto slot, const QKeySequence& sc = QKeySequence()) {
        auto* a = new QAction(text, this);
        if (!sc.isEmpty()) a->setShortcut(sc);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const auto withEditor = [this](auto fn) {
        return [this, fn] { if (CodeEditor* e = currentEditor()) fn(e); };
    };

    // File
    m_actNew = act(tr("&New File"), [this] { newFile(); }, QKeySequence::New);
    m_actOpenFile = act(tr("&Open File..."), [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Open File"), m_root);
        for (const QString& f : files) openFile(f);
    }, QKeySequence::Open);
    m_actOpenFolder = act(tr("Open &Folder / CMake Project..."), [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder"), m_root);
        if (!dir.isEmpty()) openFolder(dir);
    }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    m_actSave = act(tr("&Save"), [this] { if (auto* e = currentEditor()) saveEditor(e); }, QKeySequence::Save);
    m_actSaveAs = act(tr("Save &As..."), [this] { if (auto* e = currentEditor()) saveEditor(e, true); },
                      QKeySequence::SaveAs);
    m_actSaveAll = act(tr("Save A&ll"), [this] { saveAllModified(); }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_actClose = act(tr("&Close Tab"), [this] { if (m_tabs->count()) closeTab(m_tabs->currentIndex()); },
                     QKeySequence(Qt::CTRL | Qt::Key_W));
    m_actExit = act(tr("E&xit"), [this] { close(); }, QKeySequence(Qt::ALT | Qt::Key_F4));

    // Edit
    m_actUndo = act(tr("&Undo"), withEditor([](CodeEditor* e) { e->undo(); }), QKeySequence::Undo);
    m_actRedo = act(tr("&Redo"), withEditor([](CodeEditor* e) { e->redo(); }), QKeySequence::Redo);
    m_actCut = act(tr("Cu&t"), withEditor([](CodeEditor* e) { e->cut(); }), QKeySequence::Cut);
    m_actCopy = act(tr("&Copy"), withEditor([](CodeEditor* e) { e->copy(); }), QKeySequence::Copy);
    m_actPaste = act(tr("&Paste"), withEditor([](CodeEditor* e) { e->paste(); }), QKeySequence::Paste);
    m_actSelectAll = act(tr("Select &All"), withEditor([](CodeEditor* e) { e->selectAll(); }), QKeySequence::SelectAll);
    m_actFind = act(tr("&Find..."), [this] { if (currentEditor()) m_findBar->activate(false); }, QKeySequence::Find);
    m_actReplace = act(tr("&Replace..."), [this] { if (currentEditor()) m_findBar->activate(true); },
                       QKeySequence(Qt::CTRL | Qt::Key_H));
    m_actFindInFiles = act(tr("Find in Fi&les..."), [this] {
        m_searchDock->show();
        m_searchDock->raise();
        QString seed;
        if (auto* e = currentEditor(); e && e->textCursor().hasSelection()) seed = e->textCursor().selectedText();
        m_search->focusQuery(seed);
    }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    m_actQuickOpen = act(tr("Go to &File..."), [this] { quickOpen(); }, QKeySequence(Qt::CTRL | Qt::Key_P));
    m_actGoTo = act(tr("&Go to Line..."), [this] {
        auto* e = currentEditor();
        if (!e) return;
        bool ok = false;
        const int line = QInputDialog::getInt(this, tr("Go to Line"), tr("Line number:"),
                                              e->textCursor().blockNumber() + 1, 1, e->blockCount(), 1, &ok);
        if (ok) e->goTo(line);
    }, QKeySequence(Qt::CTRL | Qt::Key_G));
    m_actComment = act(tr("Toggle Line &Comment"), withEditor([](CodeEditor* e) { e->toggleComment(); }),
                       QKeySequence(Qt::CTRL | Qt::Key_Slash));

    // View
    m_actZoomIn = act(tr("Zoom &In"), [] { CodeEditor::zoomAll(1); }, QKeySequence::ZoomIn);
    m_actZoomOut = act(tr("Zoom &Out"), [] { CodeEditor::zoomAll(-1); }, QKeySequence::ZoomOut);
    m_actZoomReset = act(tr("&Reset Zoom"), [] { CodeEditor::resetZoomAll(); }, QKeySequence(Qt::CTRL | Qt::Key_0));

    m_themeGroup = new QActionGroup(this);
    m_actThemeDark = act(tr("Dark Grey"), [this] { setTheme(ThemeManager::DarkGrey); });
    m_actThemeLight = act(tr("Light"), [this] { setTheme(ThemeManager::Light); });
    m_actThemeCustom = act(tr("Custom Theme"), [this] { setTheme(ThemeManager::Custom); });
    m_actEditTheme = act(tr("Customize Theme..."), [this] { ThemeEditorDialog(this).exec(); });
    for (QAction* a : {m_actThemeDark, m_actThemeLight, m_actThemeCustom}) {
        a->setCheckable(true);
        m_themeGroup->addAction(a);
    }
    const auto curTheme = ThemeManager::instance().current();
    (curTheme == ThemeManager::Custom ? m_actThemeCustom : ThemeManager::instance().isDark() ? m_actThemeDark : m_actThemeLight)->setChecked(true);

    m_actWordWrap = act(tr("&Word Wrap"), [this] {
        const bool on = m_actWordWrap->isChecked();
        QSettings().setValue(QStringLiteral("editor/wordWrap"), on);
        for (int i = 0; i < m_tabs->count(); ++i)      // only the wrap mode: zoom and font stay as they are
            if (CodeEditor* ed = editorAt(i)) ed->setWordWrap(on);
    });
    m_actWordWrap->setCheckable(true);
    m_actMdPreview = act(tr("Markdown &Preview"), [this] {
        m_mdDock->setVisible(!m_mdDock->isVisible());
    }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    m_actResetLayout = act(tr("Reset Panel &Sizes"), [this] { resetPanelSizes(); });

    // Source control
    m_actSourceControl = act(tr("&Source Control"), [this] {
        m_gitDock->show();
        m_gitDock->raise();
        m_git->requestRefresh(0);
    }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));

    // Web
    m_actLiveToggle = act(tr("Start / Stop &Live Server"), [this] { toggleLive(); }, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L));
    m_actLiveOpen = act(tr("Open Live Preview in &Browser"), [this] { openLiveInBrowser(); }, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));
    m_actBrowserFile = act(tr("Open Current File in Browser (no server)"), [this] {
        CodeEditor* e = currentEditor();
        if (e && !e->isUntitled()) QDesktopServices::openUrl(QUrl::fromLocalFile(e->filePath()));
    });

    // Terminal
    m_actNewTerminal = act(tr("&New Terminal"), [this] {
        m_terminals->newTerminal();
        m_terminalDock->show();
        m_terminalDock->raise();
        m_terminals->focusCurrent();
    }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft));
    m_actToggleTerminal = act(tr("&Toggle Terminal"), [this] {
        if (m_terminalDock->isVisible() && m_terminals->isAncestorOf(QApplication::focusWidget())) {
            m_terminalDock->hide();
        } else {
            if (m_terminals->count() == 0) m_terminals->newTerminal();
            m_terminalDock->show();
            m_terminalDock->raise();
            m_terminals->focusCurrent();
        }
    }, QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    m_actCloseTerminal = act(tr("&Kill Active Terminal"), [this] { m_terminals->closeCurrent(); });
    m_actCloseAllTerminals = act(tr("Kill &All Terminals"), [this] { m_terminals->closeAll(); });

    // Build
    m_actConfigure = act(tr("&Configure CMake"), [this] { saveAllModified(); m_build->configure(); });
    m_actBuild = act(tr("&Build"), [this] { m_runAfterBuild = false; m_build->build(); },
                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    m_actRebuild = act(tr("&Rebuild"), [this] { m_runAfterBuild = false; m_build->rebuild(); });
    m_actClean = act(tr("C&lean"), [this] { m_build->clean(); });
    m_actRunFile = act(tr("Run Current &File"), [this] {
        CodeEditor* e = currentEditor();
        if (!e || e->isUntitled()) return;
        if (e->document()->isModified()) saveEditor(e);
        runFile(e->filePath());
    }, QKeySequence(Qt::CTRL | Qt::Key_F5));
    m_actStop = act(tr("&Stop"), [this] { m_runAfterBuild = false; m_build->stop(); }, QKeySequence(Qt::SHIFT | Qt::Key_F5));
    m_actRun = act(tr("&Run"), [this] { runStartup(); }, QKeySequence(Qt::Key_F5));

    // Projects, snippets, language server
    m_actNewProject = act(tr("New &Project..."), [this] { newProject(); }, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    m_actTemplates = act(tr("Project &Templates..."), [this] { TemplateManagerDialog(m_root, this).exec(); });
    m_actSnippets = act(tr("&Snippets..."), [this] { SnippetDialog(this).exec(); });
    m_actInsertSnippet = act(tr("Insert S&nippet..."), [this] { insertSnippetDialog(); }, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J));
    m_actGotoDef = act(tr("Go to &Definition"), withEditor([this](CodeEditor* e) { m_lsp->gotoDefinition(e); }), QKeySequence(Qt::Key_F12));
    m_actHover = act(tr("Show &Symbol Info"), withEditor([this](CodeEditor* e) { m_lsp->hover(e); }),
                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space));
    m_actPerf = act(tr("&Performance Monitor"), [this] {
        QSettings().setValue(QStringLiteral("perf/enabled"), m_actPerf->isChecked());
        m_perf->setEnabled(m_actPerf->isChecked());
        m_perfLabel->setVisible(m_actPerf->isChecked());
    });
    m_actPerf->setCheckable(true);
    m_actCheckUpdates = act(tr("Check for &Updates..."), [this] { checkForUpdates(true); });

    // Tools / Help
    m_actSettings = act(tr("&Settings..."), [this] {
        SettingsDialog dlg(m_ai, this);
        connect(&dlg, &SettingsDialog::checkForUpdatesRequested, this, [this] { checkForUpdates(true); });
        if (dlg.exec() == QDialog::Accepted) applyEditorSettings();
    }, QKeySequence(Qt::CTRL | Qt::Key_Comma));
    m_actAbout = act(tr("&About CrucibleCode"), [this] { showAbout(); });
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actNew);
    file->addAction(m_actNewProject);
    file->addAction(m_actOpenFile);
    file->addAction(m_actOpenFolder);
    m_recentMenu = file->addMenu(tr("Recent &Folders"));
    file->addSeparator();
    file->addAction(m_actSave);
    file->addAction(m_actSaveAs);
    file->addAction(m_actSaveAll);
    file->addSeparator();
    file->addAction(m_actClose);
    file->addAction(m_actExit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actUndo);
    edit->addAction(m_actRedo);
    edit->addSeparator();
    edit->addAction(m_actCut);
    edit->addAction(m_actCopy);
    edit->addAction(m_actPaste);
    edit->addAction(m_actSelectAll);
    edit->addSeparator();
    edit->addAction(m_actFind);
    edit->addAction(m_actReplace);
    edit->addAction(m_actFindInFiles);
    edit->addAction(m_actQuickOpen);
    edit->addAction(m_actGoTo);
    edit->addSeparator();
    edit->addAction(m_actComment);
    edit->addSeparator();
    edit->addAction(m_actInsertSnippet);
    edit->addAction(m_actGotoDef);
    edit->addAction(m_actHover);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->setObjectName(QStringLiteral("viewMenu"));
    view->addAction(m_actZoomIn);
    view->addAction(m_actZoomOut);
    view->addAction(m_actZoomReset);
    view->addAction(m_actWordWrap);
    view->addSeparator();
    QMenu* theme = view->addMenu(tr("&Theme"));
    theme->addAction(m_actThemeDark);
    theme->addAction(m_actThemeLight);
    theme->addAction(m_actThemeCustom);
    theme->addSeparator();
    theme->addAction(m_actEditTheme);
    view->addSeparator();

    QMenu* gitMenu = menuBar()->addMenu(tr("&Git"));
    gitMenu->addAction(m_actSourceControl);

    QMenu* webMenu = menuBar()->addMenu(tr("&Web"));
    webMenu->addAction(m_actLiveToggle);
    webMenu->addAction(m_actLiveOpen);
    webMenu->addSeparator();
    webMenu->addAction(m_actBrowserFile);

    QMenu* termMenu = menuBar()->addMenu(tr("Te&rminal"));
    termMenu->addAction(m_actNewTerminal);
    termMenu->addAction(m_actToggleTerminal);
    termMenu->addSeparator();
    termMenu->addAction(m_actCloseTerminal);
    termMenu->addAction(m_actCloseAllTerminals);

    QMenu* build = menuBar()->addMenu(tr("&Build"));
    build->addAction(m_actBuild);
    build->addAction(m_actRebuild);
    build->addAction(m_actClean);
    build->addSeparator();
    build->addAction(m_actConfigure);
    build->addSeparator();
    build->addAction(m_actRun);
    build->addAction(m_actRunFile);
    build->addAction(m_actStop);

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(m_actSnippets);
    tools->addAction(m_actTemplates);
    tools->addSeparator();
    tools->addAction(m_actSettings);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* actRepo = help->addAction(tr("CrucibleCode on &GitHub"));
    connect(actRepo, &QAction::triggered, this, [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kRepoUrl))); });
    QAction* actIssue = help->addAction(tr("&Report an Issue"));
    connect(actIssue, &QAction::triggered, this, [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kIssuesUrl))); });
    help->addSeparator();
    help->addAction(m_actCheckUpdates);
    help->addAction(m_actAbout);
    QAction* aboutQt = help->addAction(tr("About &Qt"));
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::createToolBar()
{
    QToolBar* tb = addToolBar(tr("Standard"));
    tb->setObjectName(QStringLiteral("standardToolBar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->addAction(m_actOpenFolder);
    tb->addAction(m_actSave);
    tb->addAction(m_actSaveAll);
    tb->addSeparator();

    m_configCombo = new QComboBox;
    m_configCombo->addItems({QStringLiteral("Debug"), QStringLiteral("Release"),
                             QStringLiteral("RelWithDebInfo"), QStringLiteral("MinSizeRel")});
    m_configCombo->setToolTip(tr("Build configuration"));
    connect(m_configCombo, &QComboBox::currentTextChanged, this, [this](const QString& cfg) {
        m_build->setConfiguration(cfg);
        QSettings().setValue(QStringLiteral("cmake/config"), cfg);
        refreshStartupItems();
    });
    m_build->setConfiguration(m_configCombo->currentText());
    tb->addWidget(m_configCombo);

    m_startupCombo = new QComboBox;
    m_startupCombo->setMinimumWidth(200);
    m_startupCombo->setPlaceholderText(tr("(no startup item)"));
    m_startupCombo->setToolTip(tr("Startup item (CMake: executables in the build folder, .NET: project files)"));
    tb->addWidget(m_startupCombo);

    tb->addAction(m_actRun);
    tb->addAction(m_actBuild);
    tb->addAction(m_actStop);
}

void MainWindow::createDocks()
{
    // Solution Explorer
    m_fs = new QFileSystemModel(this);
    applyExplorerFilter();
    m_tree = new QTreeView;
    m_tree->setModel(m_fs);
    m_tree->setHeaderHidden(true);
    for (int c = 1; c < m_fs->columnCount(); ++c) m_tree->hideColumn(c);
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(14);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &MainWindow::showTreeContextMenu);
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex& idx) {
        if (!m_fs->isDir(idx)) openFile(m_fs->filePath(idx));
    });

    m_explorerDock = new QDockWidget(tr("Explorer"), this);
    m_explorerDock->setObjectName(QStringLiteral("explorerDock"));
    m_explorerDock->setWidget(m_tree);
    addDockWidget(Qt::LeftDockWidgetArea, m_explorerDock);

    // Source Control (tabbed with the Explorer, like VS Code's side bar)
    m_gitPanel = new GitPanel(m_git);
    m_gitDock = new QDockWidget(tr("Source Control"), this);
    m_gitDock->setObjectName(QStringLiteral("gitDock"));
    m_gitDock->setWidget(m_gitPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_gitDock);
    tabifyDockWidget(m_explorerDock, m_gitDock);
    m_explorerDock->raise();
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::South);
    m_gitDock->setProperty("defaultSize", 260);
    connect(m_gitPanel, &GitPanel::openFileRequested, this, [this](const QString& p) { openFile(p); });
    connect(m_gitPanel, &GitPanel::showDiffRequested, this, &MainWindow::showDiffTab);

    // Output
    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(20000);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_outputDock = new QDockWidget(tr("Output"), this);
    m_outputDock->setObjectName(QStringLiteral("outputDock"));
    m_outputDock->setWidget(m_output);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

    // Error list
    m_errors = new QTreeWidget;
    m_errors->setColumnCount(4);
    m_errors->setHeaderLabels({tr("Severity"), tr("Description"), tr("File"), tr("Line")});
    m_errors->setRootIsDecorated(false);
    m_errors->setUniformRowHeights(true);
    m_errors->header()->setStretchLastSection(false);
    m_errors->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_errors->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_errors->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_errors->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_errors, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) {
        const QString path = it->data(0, Qt::UserRole).toString();
        if (!path.isEmpty() && QFileInfo::exists(path))
            openFile(path, it->data(0, Qt::UserRole + 1).toInt(), it->data(0, Qt::UserRole + 2).toInt());
    });
    m_errorDock = new QDockWidget(tr("Error List"), this);
    m_errorDock->setObjectName(QStringLiteral("errorDock"));
    m_errorDock->setWidget(m_errors);
    addDockWidget(Qt::BottomDockWidgetArea, m_errorDock);

    // Problems reported by language servers
    m_problems = new QTreeWidget;
    m_problems->setColumnCount(4);
    m_problems->setHeaderLabels({tr("Severity"), tr("Description"), tr("File"), tr("Line")});
    m_problems->setRootIsDecorated(false);
    m_problems->setUniformRowHeights(true);
    m_problems->header()->setStretchLastSection(false);
    m_problems->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_problems->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_problems->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_problems->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_problems, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) {
        openFile(it->data(0, Qt::UserRole).toString(), it->data(0, Qt::UserRole + 1).toInt(), it->data(0, Qt::UserRole + 2).toInt());
    });
    m_problemsDock = new QDockWidget(tr("Problems"), this);
    m_problemsDock->setObjectName(QStringLiteral("problemsDock"));
    m_problemsDock->setWidget(m_problems);
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);

    // Find in files
    m_search = new SearchPanel;
    connect(m_search, &SearchPanel::openRequested, this, [this](const QString& p, int l, int c) { openFile(p, l, c); });
    m_searchDock = new QDockWidget(tr("Find Results"), this);
    m_searchDock->setObjectName(QStringLiteral("searchDock"));
    m_searchDock->setWidget(m_search);
    addDockWidget(Qt::BottomDockWidgetArea, m_searchDock);

    // Terminal
    m_terminals = new TerminalPanel;
    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName(QStringLiteral("terminalDock"));
    m_terminalDock->setWidget(m_terminals);
    addDockWidget(Qt::BottomDockWidgetArea, m_terminalDock);
    connect(m_terminalDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible || m_terminals->count() != 0) return;
        // Deferred so that actions which create a terminal themselves do not end up with two.
        QTimer::singleShot(0, this, [this] {
            if (m_terminals->count() == 0 && m_terminalDock->isVisible()) m_terminals->newTerminal();
        });
    });

    // Markdown preview (right side, hidden until requested)
    m_mdView = new QTextBrowser;
    m_mdView->setOpenExternalLinks(true);
    m_mdDock = new QDockWidget(tr("Markdown Preview"), this);
    m_mdDock->setObjectName(QStringLiteral("markdownDock"));
    m_mdDock->setWidget(m_mdView);
    addDockWidget(Qt::RightDockWidgetArea, m_mdDock);
    m_mdDock->hide();
    m_mdTimer = new QTimer(this);
    m_mdTimer->setSingleShot(true);
    m_mdTimer->setInterval(300);
    connect(m_mdTimer, &QTimer::timeout, this, &MainWindow::updateMarkdownPreview);
    connect(m_mdDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        m_mdButton->setChecked(visible);
        updateMarkdownPreview();
    });

    tabifyDockWidget(m_outputDock, m_errorDock);
    tabifyDockWidget(m_errorDock, m_problemsDock);
    tabifyDockWidget(m_problemsDock, m_searchDock);
    tabifyDockWidget(m_searchDock, m_terminalDock);
    m_outputDock->raise();

    // Default sizes: used by "double-click a splitter" and View > Reset Panel Sizes.
    m_explorerDock->setProperty("defaultSize", 260);
    m_mdDock->setProperty("defaultSize", 420);
    for (QDockWidget* d : {m_outputDock, m_errorDock, m_problemsDock, m_searchDock, m_terminalDock})
        d->setProperty("defaultSize", 220);
    resetPanelSizes();

    // View menu: dock toggles
    if (QMenu* view = menuBar()->findChild<QMenu*>(QStringLiteral("viewMenu"))) {
        view->addAction(m_explorerDock->toggleViewAction());
        view->addAction(m_gitDock->toggleViewAction());
        view->addAction(m_outputDock->toggleViewAction());
        view->addAction(m_errorDock->toggleViewAction());
        view->addAction(m_problemsDock->toggleViewAction());
        view->addAction(m_searchDock->toggleViewAction());
        view->addAction(m_terminalDock->toggleViewAction());
        view->addAction(m_mdDock->toggleViewAction());
        view->addSeparator();
        view->addAction(m_actMdPreview);
        view->addAction(m_actPerf);
        view->addAction(m_actResetLayout);
    }
}

void MainWindow::createStatusBar()
{
    m_posLabel = new QLabel;
    m_eolLabel = new QLabel;
    m_encLabel = new QLabel(QStringLiteral("UTF-8"));
    m_perfLabel = new QLabel;
    m_perfLabel->hide();
    statusBar()->addPermanentWidget(m_perfLabel);
    m_lspLabel = new QLabel;
    m_lspLabel->hide();
    statusBar()->addPermanentWidget(m_lspLabel);
    m_gitLabel = new QLabel;
    m_gitLabel->hide();
    statusBar()->addPermanentWidget(m_gitLabel);
    m_liveLabel = new QLabel;
    m_liveLabel->hide();
    statusBar()->addPermanentWidget(m_liveLabel);
    m_aiLabel = new QLabel;
    m_aiLabel->hide();
    statusBar()->addPermanentWidget(m_aiLabel);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_eolLabel);
    statusBar()->addPermanentWidget(m_encLabel);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->showMessage(tr("Ready"));
}

// ---------------------------------------------------------------------------------
// Documents
// ---------------------------------------------------------------------------------

CodeEditor* MainWindow::currentEditor() const
{
    return qobject_cast<CodeEditor*>(m_tabs->currentWidget());
}

CodeEditor* MainWindow::newEditor()
{
    auto* e = new CodeEditor;
    e->setEditorFont(SettingsDialog::editorFont());
    e->setTabSize(SettingsDialog::tabSize());
    e->setCaretBlinkMode(SettingsDialog::caretBlinkMode());
    e->setSuggestionsEnabled(SettingsDialog::suggestionsEnabled());
    e->setSpellCheckEnabled(SettingsDialog::spellCheckEnabled());
    e->setAiCompletion(m_ai, AiCompletion::enabledInSettings());
    e->setWordWrap(m_actWordWrap && m_actWordWrap->isChecked());
    connect(e, &QPlainTextEdit::textChanged, this, [this, e] {
        if (e == currentEditor() && e->isMarkdown() && m_mdDock->isVisible()) m_mdTimer->start();
    });
    connect(e->document(), &QTextDocument::modificationChanged, this, [this, e] { updateTabTitle(e); });
    connect(e, &CodeEditor::cursorInfoChanged, this, [this, e](int, int) {
        if (e == currentEditor()) updateStatus();
    });
    return e;
}

void MainWindow::newFile()
{
    CodeEditor* e = newEditor();
    e->setUntitledName(tr("Untitled-%1").arg(++m_untitledCounter));
    const int idx = m_tabs->addTab(e, e->displayName());
    m_tabs->setCurrentIndex(idx);
    e->setFocus();
}

CodeEditor* MainWindow::openFile(const QString& path, int line, int column)
{
    const QString abs = QFileInfo(path).absoluteFilePath();

    const int existing = tabIndexForPath(abs);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        if (CodeEditor* e = editorAt(existing)) {
            if (line > 0) e->goTo(line, column > 0 ? column : 1);
            return e;
        }
        return nullptr;
    }

    switch (FileKinds::classify(abs)) {
    case FileKinds::Image: {
        auto* v = new ImageViewer(abs);
        if (!v->ok()) {
            const QString err = v->errorText();
            delete v;
            showExternal(abs, tr("This image could not be decoded: %1").arg(err), false);
            return nullptr;
        }
        addViewerTab(v, abs);
        return nullptr;
    }
    case FileKinds::Pdf:
#ifdef CRUCIBLE_HAVE_PDF
    {
        auto* v = new PdfViewer(abs);
        if (!v->ok()) {
            const QString err = v->errorText();
            delete v;
            showExternal(abs, err, false);
            return nullptr;
        }
        addViewerTab(v, abs);
        return nullptr;
    }
#else
        break;
#endif
    case FileKinds::External: {
        const QString ext = QFileInfo(abs).suffix().toLower();
        QString note;
        if (ext == QLatin1String("pdf"))
            note = tr("PDF preview needs the Qt PDF module, which this build does not include.");
        else if (ext == QLatin1String("webp") || ext == QLatin1String("tif") || ext == QLatin1String("tiff") || ext == QLatin1String("icns")
                 || ext == QLatin1String("avif") || ext == QLatin1String("heic"))
            note = tr("This Qt build has no decoder for the .%1 image format (available: %2).").arg(ext, FileKinds::imageFormatsHint());
        showExternal(abs, note, true);
        return nullptr;
    }
    case FileKinds::Text:
        break;
    }

    // Unknown extension: binary content is shown in an info page, text opens in the editor.
    QFile probe(abs);
    if (probe.open(QIODevice::ReadOnly) && probe.read(8192).contains('\0')) {
        showExternal(abs, tr("This looks like a binary file."), true);
        return nullptr;
    }
    return openTextEditor(abs, line, column);
}

CodeEditor* MainWindow::openTextEditor(const QString& abs, int line, int column)
{
    CodeEditor* e = newEditor();
    QString err;
    if (!e->loadFile(abs, &err)) {
        QMessageBox::warning(this, tr("Open File"), tr("Could not open %1:\n%2").arg(abs, err));
        delete e;
        return nullptr;
    }
    const int idx = m_tabs->addTab(e, e->displayName());
    watchEditor(e);
    refreshGitMarks(e);
    m_lsp->attach(e);
    m_tabs->setTabToolTip(idx, abs);
    m_tabs->setCurrentIndex(idx);
    if (line > 0) e->goTo(line, column > 0 ? column : 1);
    else e->setFocus();
    updateMarkdownButton();
    return e;
}

void MainWindow::addViewerTab(QWidget* viewer, const QString& path)
{
    viewer->setProperty("filePath", path);
    const int idx = m_tabs->addTab(viewer, QFileInfo(path).fileName());
    m_tabs->setTabToolTip(idx, path);
    m_tabs->setCurrentIndex(idx);
    updateMarkdownButton();
}

void MainWindow::showExternal(const QString& path, const QString& note, bool offerText)
{
    auto* v = new ExternalFileView(path, note, offerText);
    connect(v, &ExternalFileView::openAsTextRequested, this, &MainWindow::openAsText);
    addViewerTab(v, path);
}

void MainWindow::openAsText(const QString& path)
{
    const int idx = tabIndexForPath(path);
    if (idx >= 0) {
        QWidget* w = m_tabs->widget(idx);
        m_tabs->removeTab(idx);
        w->deleteLater();
    }
    openTextEditor(path, -1, -1);
}

CodeEditor* MainWindow::editorAt(int index) const
{
    return qobject_cast<CodeEditor*>(m_tabs->widget(index));
}

QString MainWindow::tabFilePath(int index) const
{
    if (CodeEditor* e = editorAt(index)) return e->filePath();
    QWidget* w = m_tabs->widget(index);
    return w ? w->property("filePath").toString() : QString();
}

int MainWindow::tabIndexForPath(const QString& path) const
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        const QString p = tabFilePath(i);
        if (!p.isEmpty() && QFileInfo(p) == QFileInfo(path)) return i;
    }
    return -1;
}

void MainWindow::updateMarkdownButton()
{
    if (!m_mdButton) return;
    CodeEditor* e = currentEditor();
    m_mdButton->setVisible(e && e->isMarkdown());
    updateLiveUi();
}

void MainWindow::updateLiveUi()
{
    if (!m_liveButton || !m_live) return;
    CodeEditor* e = currentEditor();
    const bool web = e && e->isWebFile();
    const bool running = m_live->isRunning();
    const bool starting = m_live->isStarting();
    m_liveButton->setVisible(web || running || starting);
    m_liveButton->setChecked(running || starting);
    m_liveButton->setText(running ? tr("Live: ON (:%1)").arg(m_live->port()) : starting ? tr("Starting...") : tr("Go Live"));
    m_liveOpenButton->setVisible(running);
    m_liveLabel->setVisible(running);
    if (running) m_liveLabel->setText(tr("Live: %1").arg(m_live->rootUrl().toString()));
    m_actLiveOpen->setEnabled(running);
}

void MainWindow::toggleLive()
{
    if (m_live->isRunning() || m_live->isStarting()) {
        m_live->stop();
        statusBar()->showMessage(tr("Live server stopped."), 4000);
        return;
    }
    CodeEditor* e = currentEditor();
    const QString file = (e && !e->isUntitled()) ? QFileInfo(e->filePath()).absoluteFilePath() : QString();
    QString root = m_root;
    if (root.isEmpty() || (!file.isEmpty() && !file.startsWith(root + QLatin1Char('/'))))
        root = file.isEmpty() ? root : QFileInfo(file).absolutePath();
    if (root.isEmpty()) {
        QMessageBox::information(this, tr("Live Preview"), tr("Open a folder (File > Open Folder) or save the file first."));
        return;
    }
    if (e && e->document()->isModified() && !e->isUntitled()) saveEditor(e);

    QString err;
    if (!m_live->start(root, SettingsDialog::livePort(), &err)) {
        QMessageBox::warning(this, tr("Live Preview"), err);
        return;
    }
    m_outputDock->show();
    m_outputDock->raise();
}

void MainWindow::openLiveInBrowser()
{
    if (!m_live->isRunning()) return;
    CodeEditor* e = currentEditor();
    QDesktopServices::openUrl(m_live->urlFor(e ? e->filePath() : QString()));
}

void MainWindow::updateAiLabel()
{
    if (!m_aiLabel) return;
    const bool on = AiCompletion::enabledInSettings();
    m_aiLabel->setVisible(on);
    if (on) m_aiLabel->setText(m_ai->statusText());
}

void MainWindow::runFile(const QString& path)
{
    const QString shell = m_terminals->defaultShellProgram();
    if (shell.isEmpty()) {
        QMessageBox::warning(this, tr("Run"), tr("No shell was found to run the file in."));
        return;
    }
    QString err;
    const QString cmd = RunHelper::commandFor(path, shell, &err);
    if (cmd.isEmpty()) {
        QMessageBox::warning(this, tr("Run"), err);
        return;
    }
    m_terminals->runCommand(tr("run: %1").arg(QFileInfo(path).fileName()), cmd, QFileInfo(path).absolutePath());
    m_terminalDock->show();
    m_terminalDock->raise();
    m_terminals->focusCurrent();
}

bool MainWindow::saveEditor(CodeEditor* e, bool saveAs)
{
    QString path = e->filePath();
    if (path.isEmpty() || saveAs) {
        const QString start = path.isEmpty() ? QDir(m_root.isEmpty() ? QDir::homePath() : m_root).filePath(e->displayName())
                                             : path;
        path = QFileDialog::getSaveFileName(this, tr("Save File"), start);
        if (path.isEmpty()) return false;
    }
    applySaveTransforms(e);
    QString err;
    if (!e->saveFile(path, &err)) {
        QMessageBox::warning(this, tr("Save File"), tr("Could not save %1:\n%2").arg(path, err));
        return false;
    }
    m_lsp->notifySaved(e);
    updateTabTitle(e);
    watchEditor(e);
    m_git->requestRefresh(200);
    const int idx = m_tabs->indexOf(e);
    if (idx >= 0) m_tabs->setTabToolTip(idx, e->filePath());
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved %1").arg(e->displayName()), 3000);
    return true;
}

void MainWindow::saveAllModified()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        CodeEditor* e = editorAt(i);
        if (e && e->document()->isModified() && !e->isUntitled()) saveEditor(e);
    }
}

bool MainWindow::maybeSave(CodeEditor* e)
{
    if (!e->document()->isModified()) return true;
    const auto r = QMessageBox::question(this, tr("Unsaved Changes"),
        tr("Save changes to %1?").arg(e->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return saveEditor(e);
    return r == QMessageBox::Discard;
}

void MainWindow::closeTab(int index)
{
    QWidget* w = m_tabs->widget(index);
    if (!w) return;
    if (auto* e = qobject_cast<CodeEditor*>(w)) {
        if (!maybeSave(e)) return;
        unwatchEditor(e);
    }
    m_tabs->removeTab(index);
    w->deleteLater();
    updateStatus();
    updateWindowTitle();
    updateMarkdownButton();
}

void MainWindow::closeTabForPath(const QString& path)
{
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        const QString p = tabFilePath(i);
        if (!p.isEmpty() && QFileInfo(p) == QFileInfo(path)) {
            QWidget* w = m_tabs->widget(i);
            m_tabs->removeTab(i);
            w->deleteLater();
        }
    }
    updateMarkdownButton();
}

void MainWindow::updateTabTitle(CodeEditor* e)
{
    const int idx = m_tabs->indexOf(e);
    if (idx < 0) return;
    m_tabs->setTabText(idx, e->displayName() + (e->document()->isModified() ? QStringLiteral(" *") : QString()));
    updateWindowTitle();
}

// ---------------------------------------------------------------------------------
// UI state
// ---------------------------------------------------------------------------------

void MainWindow::updateWindowTitle()
{
    QString t;
    if (auto* e = currentEditor()) {
        t = e->displayName();
        if (e->document()->isModified()) t += QStringLiteral("*");
        t += QStringLiteral(" - ");
    }
    if (!m_root.isEmpty()) t += QFileInfo(m_root).fileName() + QStringLiteral(" - ");
    setWindowTitle(t + QStringLiteral("CrucibleCode"));
}

void MainWindow::updateStatus()
{
    if (auto* e = currentEditor()) {
        const QTextCursor c = e->textCursor();
        m_posLabel->setText(tr("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1));
        m_eolLabel->setText(e->eol());
        m_encLabel->setText(QStringLiteral("UTF-8"));
    } else {
        m_posLabel->clear();
        m_eolLabel->clear();
        m_encLabel->clear();
    }
}

void MainWindow::updateBuildActions()
{
    const bool project = m_build->hasProject();
    const bool busy = m_build->isBusy();
    for (QAction* a : {m_actConfigure, m_actBuild, m_actRebuild, m_actClean})
        a->setEnabled(project && !busy);
    m_actRun->setEnabled(project && !busy);
    m_actStop->setEnabled(busy);
    m_configCombo->setEnabled(!busy);
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recentFolders")).toStringList();
    for (const QString& p : recent) {
        QAction* a = m_recentMenu->addAction(QDir::toNativeSeparators(p));
        connect(a, &QAction::triggered, this, [this, p] { openFolder(p); });
    }
    m_recentMenu->setEnabled(!recent.isEmpty());
}

void MainWindow::refreshStartupItems()
{
    const QString previous = m_startupCombo->currentData().toString();
    m_startupCombo->clear();
    const QList<StartupItem> items = m_build->startupItems();
    for (const StartupItem& item : items)
        m_startupCombo->addItem(item.name, item.path);
    const int idx = m_startupCombo->findData(previous);
    if (idx >= 0) m_startupCombo->setCurrentIndex(idx);
    else if (!items.isEmpty()) m_startupCombo->setCurrentIndex(0);
}

void MainWindow::applyEditorSettings()
{
    QSettings s;
    const QFont f = SettingsDialog::editorFont();
    const bool wrap = s.value(QStringLiteral("editor/wordWrap"), false).toBool();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* dv = qobject_cast<DiffViewer*>(m_tabs->widget(i))) dv->setFont(f);
        CodeEditor* e = editorAt(i);
        if (!e) continue;
        e->setEditorFont(f);   // keeps each editor's zoom level
        e->setTabSize(SettingsDialog::tabSize());
        e->setCaretBlinkMode(SettingsDialog::caretBlinkMode());
        e->setWordWrap(wrap);
        e->setSuggestionsEnabled(SettingsDialog::suggestionsEnabled());
        e->setSpellCheckEnabled(SettingsDialog::spellCheckEnabled());
        e->setAiCompletion(m_ai, AiCompletion::enabledInSettings());
    }
    m_ai->applySettings();
    updateAiLabel();
    m_output->setFont(f);
    m_terminals->setTerminalFont(f);
    applyExplorerFilter();
    m_build->setCMakePath(s.value(QStringLiteral("cmake/path")).toString());
    m_build->setGenerator(s.value(QStringLiteral("cmake/generator")).toString());
    m_build->setExtraArguments(s.value(QStringLiteral("cmake/extraArgs")).toString());

    const bool perf = SettingsDialog::perfMonitorEnabled();
    m_actPerf->setChecked(perf);
    m_perf->setEnabled(perf);
    m_perfLabel->setVisible(perf);
    m_lsp->reloadSettings();
    updateLspUi();
}

// ---------------------------------------------------------------------------------
// Projects, updates, language server, shortcuts
// ---------------------------------------------------------------------------------

void MainWindow::newProject()
{
    NewProjectDialog dlg(QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString dir = dlg.createdPath();
    openFolder(dir);
    if (m_root != QDir::cleanPath(QFileInfo(dir).absoluteFilePath())) return;      // user cancelled the unsaved-changes prompt
    for (const QString& rel : {QStringLiteral("src/main.cpp"), QStringLiteral("src/main.c"), QStringLiteral("Program.cs"),
                               QStringLiteral("index.html"), QStringLiteral("README.md")}) {
        if (QFileInfo::exists(QDir(dir).filePath(rel))) { openFile(QDir(dir).filePath(rel)); break; }
    }
}

void MainWindow::applySaveTransforms(CodeEditor* e)
{
    const bool trim = SettingsDialog::trimTrailingWhitespace(), nl = SettingsDialog::ensureFinalNewline();
    if (!trim && !nl) return;
    QTextCursor c(e->document());
    c.beginEditBlock();
    if (trim) {
        for (QTextBlock b = e->document()->begin(); b.isValid(); b = b.next()) {
            const QString t = b.text();
            int end = int(t.size());
            while (end > 0 && (t.at(end - 1) == QLatin1Char(' ') || t.at(end - 1) == QLatin1Char('\t'))) --end;
            if (end < t.size()) {
                QTextCursor tc(b);
                tc.setPosition(b.position() + end);
                tc.setPosition(b.position() + int(t.size()), QTextCursor::KeepAnchor);
                tc.removeSelectedText();
            }
        }
    }
    if (nl && !e->document()->lastBlock().text().isEmpty()) {
        QTextCursor tc(e->document());
        tc.movePosition(QTextCursor::End);
        tc.insertText(QStringLiteral("\n"));
    }
    c.endEditBlock();
}

bool MainWindow::saveOrDiscardAll()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        CodeEditor* ed = editorAt(i);
        if (!ed) continue;
        m_tabs->setCurrentIndex(i);
        if (!maybeSave(ed)) return false;
        ed->document()->setModified(false);          // discarded changes must not prompt again on close
    }
    return true;
}

void MainWindow::checkForUpdates(bool manual)
{
    if (m_updateCheckRunning) return;
    m_updateCheckRunning = true;
    if (manual) statusBar()->showMessage(tr("Checking for updates..."), 4000);
    disconnect(m_updater, &Updater::checkFinished, nullptr, nullptr);
    connect(m_updater, &Updater::checkFinished, this,
            [this](bool wasManual, bool available, const ReleaseInfo& rel, const QString& error) {
        m_updateCheckRunning = false;
        if (!error.isEmpty()) {
            if (wasManual) QMessageBox::warning(this, tr("Check for Updates"), tr("Could not check for updates:\n%1").arg(error));
            return;
        }
        if (!available) {
            if (wasManual) QMessageBox::information(this, tr("Check for Updates"),
                tr("You are up to date (version %1).").arg(QCoreApplication::applicationVersion()));
            return;
        }
        if (!wasManual && Updater::skippedVersion() == rel.tag) return;
        showUpdateDialog(rel);
    });
    m_updater->check(manual);
}

void MainWindow::showUpdateDialog(const ReleaseInfo& release)
{
    auto* dlg = new UpdateDialog(m_updater, release, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &UpdateDialog::applyRequested, this, &MainWindow::applyUpdate);
    dlg->open();
}

void MainWindow::applyUpdate(const QString& file)
{
    if (!saveOrDiscardAll()) {
        statusBar()->showMessage(tr("Update postponed: unsaved changes."), 5000);
        return;
    }
    QString err;
    if (!m_updater->applyDownloaded(file, &err)) {
        QMessageBox::warning(this, tr("Update"), err);
        return;
    }
    close();    // the helper waits for this process to exit, replaces the files and starts the new version
}

void MainWindow::insertSnippetDialog()
{
    CodeEditor* e = currentEditor();
    if (!e) return;
    const QList<Snippet> list = SnippetManager::instance().forFile(e->isUntitled() ? e->displayName() : e->filePath());
    QStringList items;
    for (const Snippet& s : list) items << (s.description.isEmpty() ? s.trigger : s.trigger + QStringLiteral(" - ") + s.description);
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, tr("Insert Snippet"), tr("Snippet:"), items, 0, false, &ok);
    if (!ok) return;
    const int i = int(items.indexOf(pick));
    if (i >= 0) { e->insertSnippet(list.at(i).body); e->setFocus(); }
}

void MainWindow::updateLspUi()
{
    const QString t = m_lsp->statusText();
    m_lspLabel->setText(t);
    m_lspLabel->setToolTip(m_lsp->statusTooltip());
    m_lspLabel->setVisible(!t.isEmpty());
}

void MainWindow::refreshProblems()
{
    m_problems->clear();
    const ThemeColors& tc = ThemeManager::instance().colors();
    const auto problems = m_lsp->problems();
    int total = 0;
    for (auto it = problems.begin(); it != problems.end(); ++it) {
        for (const LspDiagnostic& d : it.value()) {
            if (d.severity > 3) continue;
            const QString sev = d.severity == 1 ? tr("Error") : d.severity == 2 ? tr("Warning") : tr("Info");
            auto* item = new QTreeWidgetItem({sev, d.message.section(QLatin1Char('\n'), 0, 0), QFileInfo(it.key()).fileName(), QString::number(d.line + 1)});
            item->setForeground(0, d.severity == 1 ? tc.error : d.severity == 2 ? tc.warning : tc.textDim);
            item->setData(0, Qt::UserRole, it.key());
            item->setData(0, Qt::UserRole + 1, d.line + 1);
            item->setData(0, Qt::UserRole + 2, d.startChar + 1);
            item->setToolTip(1, d.message);
            m_problems->addTopLevelItem(item);
            ++total;
        }
    }
    m_problemsDock->setWindowTitle(total ? tr("Problems (%1)").arg(total) : tr("Problems"));
}

void MainWindow::registerShortcuts()
{
    auto& sm = ShortcutManager::instance();
    const QString file = tr("File"), edit = tr("Edit"), view = tr("View"), build = tr("Build"), tools = tr("Tools"), code = tr("Code");
    sm.add(QStringLiteral("file.new"), m_actNew, file);
    sm.add(QStringLiteral("file.newProject"), m_actNewProject, file);
    sm.add(QStringLiteral("file.open"), m_actOpenFile, file);
    sm.add(QStringLiteral("file.openFolder"), m_actOpenFolder, file);
    sm.add(QStringLiteral("file.save"), m_actSave, file);
    sm.add(QStringLiteral("file.saveAs"), m_actSaveAs, file);
    sm.add(QStringLiteral("file.saveAll"), m_actSaveAll, file);
    sm.add(QStringLiteral("file.closeTab"), m_actClose, file);
    sm.add(QStringLiteral("file.exit"), m_actExit, file);
    sm.add(QStringLiteral("edit.undo"), m_actUndo, edit);
    sm.add(QStringLiteral("edit.redo"), m_actRedo, edit);
    sm.add(QStringLiteral("edit.cut"), m_actCut, edit);
    sm.add(QStringLiteral("edit.copy"), m_actCopy, edit);
    sm.add(QStringLiteral("edit.paste"), m_actPaste, edit);
    sm.add(QStringLiteral("edit.selectAll"), m_actSelectAll, edit);
    sm.add(QStringLiteral("edit.find"), m_actFind, edit);
    sm.add(QStringLiteral("edit.replace"), m_actReplace, edit);
    sm.add(QStringLiteral("edit.findInFiles"), m_actFindInFiles, edit);
    sm.add(QStringLiteral("edit.quickOpen"), m_actQuickOpen, edit);
    sm.add(QStringLiteral("edit.goToLine"), m_actGoTo, edit);
    sm.add(QStringLiteral("edit.comment"), m_actComment, edit);
    sm.add(QStringLiteral("code.insertSnippet"), m_actInsertSnippet, code);
    sm.add(QStringLiteral("code.gotoDefinition"), m_actGotoDef, code);
    sm.add(QStringLiteral("code.symbolInfo"), m_actHover, code);
    sm.add(QStringLiteral("view.zoomIn"), m_actZoomIn, view);
    sm.add(QStringLiteral("view.zoomOut"), m_actZoomOut, view);
    sm.add(QStringLiteral("view.zoomReset"), m_actZoomReset, view);
    sm.add(QStringLiteral("view.wordWrap"), m_actWordWrap, view);
    sm.add(QStringLiteral("view.markdownPreview"), m_actMdPreview, view);
    sm.add(QStringLiteral("view.perfMonitor"), m_actPerf, view);
    sm.add(QStringLiteral("view.explorer"), m_explorerDock->toggleViewAction(), view);
    sm.add(QStringLiteral("view.output"), m_outputDock->toggleViewAction(), view);
    sm.add(QStringLiteral("view.errorList"), m_errorDock->toggleViewAction(), view);
    sm.add(QStringLiteral("view.problems"), m_problemsDock->toggleViewAction(), view);
    sm.add(QStringLiteral("git.sourceControl"), m_actSourceControl, tr("Git"));
    sm.add(QStringLiteral("web.liveToggle"), m_actLiveToggle, tr("Web"));
    sm.add(QStringLiteral("web.liveOpen"), m_actLiveOpen, tr("Web"));
    sm.add(QStringLiteral("terminal.new"), m_actNewTerminal, tr("Terminal"));
    sm.add(QStringLiteral("terminal.toggle"), m_actToggleTerminal, tr("Terminal"));
    sm.add(QStringLiteral("build.configure"), m_actConfigure, build);
    sm.add(QStringLiteral("build.build"), m_actBuild, build);
    sm.add(QStringLiteral("build.rebuild"), m_actRebuild, build);
    sm.add(QStringLiteral("build.clean"), m_actClean, build);
    sm.add(QStringLiteral("build.run"), m_actRun, build);
    sm.add(QStringLiteral("build.runFile"), m_actRunFile, build);
    sm.add(QStringLiteral("build.stop"), m_actStop, build);
    sm.add(QStringLiteral("tools.settings"), m_actSettings, tools);
    sm.add(QStringLiteral("tools.snippets"), m_actSnippets, tools);
    sm.add(QStringLiteral("tools.templates"), m_actTemplates, tools);
    sm.add(QStringLiteral("help.checkUpdates"), m_actCheckUpdates, tr("Help"));
}

void MainWindow::setTheme(ThemeManager::Id id)
{
    QSettings().setValue(QStringLiteral("theme"), ThemeManager::idToName(id));
    ThemeManager::instance().apply(id);
}

// ---------------------------------------------------------------------------------
// Folder / project
// ---------------------------------------------------------------------------------

void MainWindow::openFolder(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.isDir()) return;
    const QString newRoot = QDir::cleanPath(fi.absoluteFilePath());
    const bool switching = newRoot != m_root;

    if (switching) {
        // Only the current project's tabs are shown: remember the old ones, then close them.
        if (!m_root.isEmpty()) saveSession();
        if (!closeAllEditors()) return;   // user cancelled an unsaved-changes prompt
        m_terminals->closeAll();
        m_live->stop();
        m_output->clear();
        m_errors->clear();
        m_errorDock->setWindowTitle(tr("Error List"));
        m_findBar->hide();
    }
    m_root = newRoot;
    m_lsp->setProjectRoot(m_root);

    const QModelIndex rootIdx = m_fs->setRootPath(m_root);
    m_tree->setRootIndex(rootIdx);
    m_explorerDock->setWindowTitle(tr("Explorer - %1").arg(QFileInfo(m_root).fileName()));

    m_build->setProjectRoot(m_root);
    m_git->setRoot(m_root);
    m_terminals->setWorkingDirectory(m_root);
    m_search->setRoot(m_root, m_build->buildRoot());
    refreshStartupItems();

    QSettings s;
    QStringList recent = s.value(QStringLiteral("recentFolders")).toStringList();
    recent.removeAll(m_root);
    recent.prepend(m_root);
    while (recent.size() > kMaxRecent) recent.removeLast();
    s.setValue(QStringLiteral("recentFolders"), recent);
    s.setValue(QStringLiteral("lastFolder"), m_root);
    updateRecentMenu();

    if (switching) restoreSession();   // reopen the tabs this project had last time

    updateBuildActions();
    updateWindowTitle();
    updateMarkdownPreview();
    statusBar()->showMessage(m_build->hasProject()
        ? tr("%1 project opened: %2").arg(m_build->projectKindName(), m_root)
        : tr("Folder opened (no CMakeLists.txt, .sln or .csproj found): %1").arg(m_root));
}

bool MainWindow::closeAllEditors()
{
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        QWidget* w = m_tabs->widget(i);
        m_tabs->setCurrentIndex(i);
        if (auto* e = qobject_cast<CodeEditor*>(w)) {
            if (!maybeSave(e)) return false;
            unwatchEditor(e);
        }
        m_tabs->removeTab(i);
        w->deleteLater();
    }
    updateStatus();
    updateWindowTitle();
    updateMarkdownButton();
    return true;
}

QString MainWindow::sessionKey(const QString& root)
{
    return QStringLiteral("sessions/")
         + QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Md5).toHex());
}

void MainWindow::saveSession()
{
    if (m_root.isEmpty()) return;
    QStringList files;
    QString current;
    for (int i = 0; i < m_tabs->count(); ++i) {
        const QString path = tabFilePath(i);
        if (path.isEmpty()) continue;
        int line = 0, col = 0;
        if (CodeEditor* e = editorAt(i)) {
            const QTextCursor c = e->textCursor();
            line = c.blockNumber() + 1;
            col = c.positionInBlock() + 1;
        }
        files << QStringLiteral("%1|%2|%3").arg(path).arg(line).arg(col);
        if (i == m_tabs->currentIndex()) current = path;
    }
    QSettings s;
    const QString k = sessionKey(m_root);
    s.setValue(k + QStringLiteral("/files"), files);
    s.setValue(k + QStringLiteral("/current"), current);
}

void MainWindow::restoreSession()
{
    QSettings s;
    const QString k = sessionKey(m_root);
    const QStringList files = s.value(k + QStringLiteral("/files")).toStringList();
    const QString current = s.value(k + QStringLiteral("/current")).toString();
    for (const QString& entry : files) {
        const int p2 = int(entry.lastIndexOf(QLatin1Char('|')));
        const int p1 = p2 > 0 ? int(entry.lastIndexOf(QLatin1Char('|'), p2 - 1)) : -1;
        if (p1 <= 0 || p2 <= p1) continue;
        const QString file = entry.left(p1);
        if (!QFileInfo::exists(file)) continue;
        openFile(file, entry.mid(p1 + 1, p2 - p1 - 1).toInt(), entry.mid(p2 + 1).toInt());
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (tabFilePath(i) == current) {
            m_tabs->setCurrentIndex(i);
            break;
        }
    }
}

void MainWindow::restoreLastSession()
{
    const QString last = QSettings().value(QStringLiteral("lastFolder")).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir()) openFolder(last);
}

void MainWindow::showTreeContextMenu(const QPoint& pos)
{
    if (m_root.isEmpty()) return;
    const QModelIndex idx = m_tree->indexAt(pos);
    const QString path = idx.isValid() ? m_fs->filePath(idx) : m_root;
    const QFileInfo fi(path);
    const QString dir = fi.isDir() ? path : fi.absolutePath();

    QMenu menu(this);
    QAction* aRun = nullptr;
    if (!fi.isDir() && RunHelper::isRunnable(path)) {
        aRun = menu.addAction(tr("Run"));
        menu.addSeparator();
    }
    QAction* aNewFile = menu.addAction(tr("New File..."));
    QAction* aNewDir = menu.addAction(tr("New Folder..."));
    QAction *aRename = nullptr, *aDelete = nullptr;
    if (idx.isValid()) {
        menu.addSeparator();
        aRename = menu.addAction(tr("Rename..."));
        aDelete = menu.addAction(tr("Delete"));
    }
    menu.addSeparator();
    QAction* aReveal = menu.addAction(tr("Open in File Manager"));
    QAction* aTerminal = menu.addAction(tr("Open in Terminal"));
    QAction* aCopyPath = menu.addAction(tr("Copy Path"));

    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (aRun && chosen == aRun) {
        runFile(path);
    } else if (chosen == aNewFile) {
        const QString name = QInputDialog::getText(this, tr("New File"), tr("File name:"));
        if (name.trimmed().isEmpty()) return;
        const QString target = QDir(dir).filePath(name.trimmed());
        if (QFileInfo::exists(target)) {
            QMessageBox::warning(this, tr("New File"), tr("%1 already exists.").arg(target));
            return;
        }
        QFile f(target);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!f.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, tr("New File"), f.errorString());
            return;
        }
        f.close();
        openFile(target);
    } else if (chosen == aNewDir) {
        const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"));
        if (!name.trimmed().isEmpty() && !QDir(dir).mkpath(name.trimmed()))
            QMessageBox::warning(this, tr("New Folder"), tr("Could not create the folder."));
    } else if (chosen == aRename) {
        const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal, fi.fileName());
        if (name.trimmed().isEmpty() || name == fi.fileName()) return;
        const QString target = QDir(fi.absolutePath()).filePath(name.trimmed());
        if (!QFile::rename(path, target)) {
            QMessageBox::warning(this, tr("Rename"), tr("Could not rename %1.").arg(fi.fileName()));
            return;
        }
        closeTabForPath(path);
    } else if (chosen == aDelete) {
        const auto r = QMessageBox::warning(this, tr("Delete"),
            tr("Permanently delete %1?").arg(fi.fileName()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
        const bool ok = fi.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
        if (!ok) QMessageBox::warning(this, tr("Delete"), tr("Could not delete %1.").arg(fi.fileName()));
        else closeTabForPath(path);
    } else if (chosen == aReveal) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    } else if (chosen == aTerminal) {
        m_terminals->newTerminal(dir);
        m_terminalDock->show();
        m_terminalDock->raise();
        m_terminals->focusCurrent();
    } else if (chosen == aCopyPath) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    }
}

// ---------------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------------

void MainWindow::appendOutput(const QString& text, BuildManager::Kind kind)
{
    const ThemeColors& c = ThemeManager::instance().colors();
    QTextCharFormat fmt;
    switch (kind) {
    case BuildManager::Error:   fmt.setForeground(c.error); break;
    case BuildManager::Warning: fmt.setForeground(c.warning); break;
    case BuildManager::Success: fmt.setForeground(c.success); break;
    case BuildManager::Info:    fmt.setForeground(ThemeManager::instance().isDark() ? c.accent.lighter(140) : c.accent); break;
    case BuildManager::Normal:  fmt.setForeground(c.text); break;
    }
    QTextCursor cur(m_output->document());
    cur.movePosition(QTextCursor::End);
    if (!m_output->document()->isEmpty()) cur.insertBlock();
    cur.insertText(text, fmt);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void MainWindow::addDiagnostic(const Diagnostic& d)
{
    const ThemeColors& c = ThemeManager::instance().colors();
    auto* it = new QTreeWidgetItem(m_errors);
    const bool isErr = d.severity == Diagnostic::Error;
    it->setText(0, isErr ? tr("Error") : tr("Warning"));
    it->setForeground(0, isErr ? c.error : c.warning);
    it->setText(1, d.message);
    it->setText(2, d.file.isEmpty() ? QString() : QDir(m_root).relativeFilePath(d.file));
    it->setText(3, d.line > 0 ? QString::number(d.line) : QString());
    it->setToolTip(1, d.message);
    it->setData(0, Qt::UserRole, d.file);
    it->setData(0, Qt::UserRole + 1, d.line);
    it->setData(0, Qt::UserRole + 2, d.column);
}

void MainWindow::onBuildStarted(const QString& title)
{
    saveAllModified();
    m_errors->clear();
    m_output->clear();
    m_outputDock->show();
    m_outputDock->raise();
    statusBar()->showMessage(title);
    updateBuildActions();
}

void MainWindow::onBuildFinished(int code, bool ok)
{
    int errors = 0, warnings = 0;
    for (int i = 0; i < m_errors->topLevelItemCount(); ++i)
        (m_errors->topLevelItem(i)->text(0) == tr("Error") ? errors : warnings)++;
    m_errorDock->setWindowTitle(tr("Error List (%1 errors, %2 warnings)").arg(errors).arg(warnings));

    if (ok) {
        appendOutput(tr("Finished successfully."), BuildManager::Success);
        statusBar()->showMessage(tr("Succeeded"), 5000);
    } else {
        appendOutput(tr("Failed (exit code %1).").arg(code), BuildManager::Error);
        statusBar()->showMessage(tr("Failed"), 5000);
        if (errors > 0) { m_errorDock->show(); m_errorDock->raise(); }
    }
    refreshStartupItems();
    updateBuildActions();

    if (m_runAfterBuild) {
        m_runAfterBuild = false;
        if (ok) {
            const QString exe = m_startupCombo->currentData().toString();
            if (exe.isEmpty())
                appendOutput(tr("No startup item: add an add_executable() target to CMakeLists.txt (or a .csproj for C#)."),
                             BuildManager::Warning);
            else
                m_build->run(exe);
        }
    }
}

void MainWindow::runStartup()
{
    m_runAfterBuild = true;
    m_build->build();
    updateBuildActions();
}

// ---------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* e)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        CodeEditor* ed = editorAt(i);
        if (!ed) continue;
        m_tabs->setCurrentIndex(i);
        if (!maybeSave(ed)) {
            e->ignore();
            return;
        }
    }
    m_build->stop();
    m_ai->stop();
    m_live->stop();
    m_lsp->shutdownAll();
    saveSession();
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        const QString p = u.toLocalFile();
        if (QFileInfo(p).isDir()) openFolder(p);
        else openFile(p);
    }
}

// ---------------------------------------------------------------------------------
// Panels: double-click a splitter to reset that panel's size
// ---------------------------------------------------------------------------------

void MainWindow::mouseDoubleClickEvent(QMouseEvent* e)
{
    // Dock separators are not widgets: a double click lands on the main window itself.
    if (e->button() == Qt::LeftButton && resetDockAt(e->position().toPoint())) {
        e->accept();
        return;
    }
    QMainWindow::mouseDoubleClickEvent(e);
}

bool MainWindow::resetDockAt(const QPoint& pos)
{
    const auto docks = findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QDockWidget* d : docks) {
        if (!d->isVisible() || d->isFloating()) continue;
        const QRect g = d->geometry();
        if (g.contains(pos)) continue;
        if (!g.adjusted(-8, -8, 8, 8).contains(pos)) continue;
        resetDockSize(d);
        return true;
    }
    return false;
}

void MainWindow::resetDockSize(QDockWidget* dock)
{
    const int size = dock->property("defaultSize").toInt();
    if (size <= 0) return;
    const Qt::DockWidgetArea area = dockWidgetArea(dock);
    const bool horizontal = area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea;
    resizeDocks({dock}, {size}, horizontal ? Qt::Horizontal : Qt::Vertical);
}

void MainWindow::resetPanelSizes()
{
    resizeDocks({m_explorerDock}, {m_explorerDock->property("defaultSize").toInt()}, Qt::Horizontal);
    resizeDocks({m_mdDock}, {m_mdDock->property("defaultSize").toInt()}, Qt::Horizontal);
    resizeDocks({m_outputDock}, {m_outputDock->property("defaultSize").toInt()}, Qt::Vertical);
}

// ---------------------------------------------------------------------------------
// Markdown preview
// ---------------------------------------------------------------------------------

void MainWindow::updateMarkdownPreview()
{
    if (!m_mdDock || !m_mdDock->isVisible()) return;
    CodeEditor* e = currentEditor();
    if (!e || !e->isMarkdown()) {
        m_mdView->setPlainText(tr("Open a Markdown file (.md) to preview it here."));
        return;
    }
    const int scroll = m_mdView->verticalScrollBar()->value();
    m_mdView->setMarkdown(e->toPlainText());
    m_mdView->verticalScrollBar()->setValue(scroll);
}

// ---------------------------------------------------------------------------------
// About
// ---------------------------------------------------------------------------------

void MainWindow::showAbout()
{
    const int extra = CodeEditor::syntaxDefinitionCount();
    const QString builtin = SyntaxHighlighter::builtinLanguages().join(QStringLiteral(", "));
    const QString more = extra > 0
        ? tr("Plus <b>%1 more languages</b> through KSyntaxHighlighting (syntax highlighting).").arg(extra)
        : tr("KSyntaxHighlighting (300+ more languages) is not part of this build: configure again with git and network access to bundle it.");

    QMessageBox box(this);
    box.setWindowTitle(tr("About CrucibleCode"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>CrucibleCode %1</h3>"
        "A lightweight, cross-platform IDE built with Qt 6 and C++.<br>"
        "Licensed under the MIT License. Qt is used under the LGPL v3 (dynamic linking)."
        "<p><b>Primary languages</b><br>"
        "&bull; <b>C</b> and <b>C++</b> &mdash; highlighting, CMake build / run, compiler diagnostics (GCC, Clang, MSVC)<br>"
        "&bull; <b>C#</b> &mdash; highlighting, .NET build / run via the <tt>dotnet</tt> CLI (.sln / .csproj), MSBuild diagnostics</p>"
        "<p><b>Also supported (built in):</b> %2.<br>%3</p>"
        "<p><a href=\"%4\">%4</a></p>"
        "<p><b>Also included:</b> image viewer (PNG, JPEG, WebP, ICO, ICNS, SVG, ...), PDF viewer, integrated native terminals, "
        "run-a-script from the Explorer, Markdown preview, spell check for Markdown / text files, optional code suggestions "
        "and optional local AI tab completion, Git source control, live preview for web projects.</p>"
        "<p><i>Disclaimer: CrucibleCode is a hobby project made for fun and learning. AI tools were used in certain areas "
        "of the software (code and documentation). It is provided as is, without any warranty.</i></p>")
        .arg(QApplication::applicationVersion(), builtin, more, QString::fromLatin1(kRepoUrl)));
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.setIconPixmap(QPixmap(QStringLiteral(":/icons/icon-64.png")));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

// ---------------------------------------------------------------------------------
// Explorer filter, quick open, external file changes, tab menu
// ---------------------------------------------------------------------------------

void MainWindow::applyExplorerFilter()
{
    QDir::Filters f = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot;
    if (SettingsDialog::showHiddenFiles()) f |= QDir::Hidden;   // dot-files such as .gitignore
    m_fs->setFilter(f);
}

void MainWindow::quickOpen()
{
    if (m_root.isEmpty()) {
        statusBar()->showMessage(tr("Open a folder first (File > Open Folder)."), 4000);
        return;
    }
    QuickOpenDialog dlg(m_root, this);
    if (dlg.exec() == QDialog::Accepted && !dlg.selectedPath().isEmpty())
        openFile(dlg.selectedPath());
}

void MainWindow::watchEditor(CodeEditor* e)
{
    if (e && !e->isUntitled() && !m_watcher->files().contains(e->filePath()))
        m_watcher->addPath(e->filePath());
}

void MainWindow::unwatchEditor(CodeEditor* e)
{
    if (e && !e->isUntitled()) m_watcher->removePath(e->filePath());
}

void MainWindow::onExternalChange(const QString& path)
{
    CodeEditor* editor = nullptr;
    for (int i = 0; i < m_tabs->count(); ++i) {
        CodeEditor* e = editorAt(i);
        if (e && !e->isUntitled() && QFileInfo(e->filePath()) == QFileInfo(path)) { editor = e; break; }
    }
    if (!editor) return;

    const QFileInfo fi(path);
    if (!fi.exists()) {
        statusBar()->showMessage(tr("%1 was deleted or moved on disk.").arg(editor->displayName()), 6000);
        return;
    }
    // Atomic saves replace the file and silently drop the watch: watch again.
    if (!m_watcher->files().contains(path)) m_watcher->addPath(path);
    if (fi.lastModified() == editor->diskTime()) return;   // our own save

    bool reload = true;
    if (editor->document()->isModified()) {
        reload = QMessageBox::question(this, tr("File Changed"),
            tr("%1 was changed outside CrucibleCode and you have unsaved edits.\nReload from disk and lose your edits?")
                .arg(editor->displayName()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }
    if (reload) {
        QString err;
        if (editor->reloadFile(&err))
            statusBar()->showMessage(tr("Reloaded %1 (changed on disk).").arg(editor->displayName()), 5000);
        else
            statusBar()->showMessage(tr("Could not reload %1: %2").arg(editor->displayName(), err), 6000);
    }
}

void MainWindow::showTabContextMenu(const QPoint& pos)
{
    const int index = m_tabs->tabBar()->tabAt(pos);
    if (index < 0) return;
    auto* e = qobject_cast<CodeEditor*>(m_tabs->widget(index));
    if (!e) return;

    QMenu menu(this);
    QAction* aSave = menu.addAction(tr("Save"));
    aSave->setEnabled(e->document()->isModified() || e->isUntitled());
    QAction* aClose = menu.addAction(tr("Close"));
    QAction* aOthers = menu.addAction(tr("Close Others"));
    aOthers->setEnabled(m_tabs->count() > 1);
    QAction* aAll = menu.addAction(tr("Close All"));
    menu.addSeparator();
    QAction* aRun = nullptr;
    if (!e->isUntitled() && RunHelper::isRunnable(e->filePath())) aRun = menu.addAction(tr("Run"));
    QAction* aCopy = menu.addAction(tr("Copy Path"));
    aCopy->setEnabled(!e->isUntitled());
    QAction* aReveal = menu.addAction(tr("Open Containing Folder"));
    aReveal->setEnabled(!e->isUntitled());

    QAction* chosen = menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
    if (!chosen) return;
    if (aRun && chosen == aRun) {
        if (e->document()->isModified()) saveEditor(e);
        runFile(e->filePath());
    } else if (chosen == aSave) saveEditor(e);
    else if (chosen == aClose) closeTab(m_tabs->indexOf(e));
    else if (chosen == aOthers) {
        for (int i = m_tabs->count() - 1; i >= 0; --i)
            if (m_tabs->widget(i) != e) { closeTab(i); if (m_tabs->indexOf(e) < 0) break; }
    } else if (chosen == aAll) {
        for (int i = m_tabs->count() - 1; i >= 0; --i) closeTab(i);
    } else if (chosen == aCopy) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(e->filePath()));
    } else if (chosen == aReveal) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(e->filePath()).absolutePath()));
    }
}

// ---------------------------------------------------------------------------------
// Git integration
// ---------------------------------------------------------------------------------

void MainWindow::updateGitUi()
{
    if (!m_gitLabel) return;
    const GitStatus& st = m_git->status();
    if (st.isRepo) {
        const QString branch = st.detached ? tr("detached") : st.branch;
        m_gitLabel->setText(QStringLiteral("git: ") + branch + (st.changes.isEmpty() ? QString() : QStringLiteral("*")));
        m_gitLabel->setToolTip(tr("%n changed file(s)", nullptr, int(st.changes.size())));
        m_gitLabel->show();
    } else {
        m_gitLabel->hide();
    }
    for (int i = 0; i < m_tabs->count(); ++i)
        if (CodeEditor* e = editorAt(i)) refreshGitMarks(e);
}

// Coloured bars in the gutter: added / modified / deleted lines compared with HEAD.
void MainWindow::refreshGitMarks(CodeEditor* e)
{
    if (!e) return;
    const GitStatus& st = m_git->status();
    if (!st.isRepo || e->isUntitled()) { e->setLineMarks({}); return; }

    const QString rel = QDir(m_git->topLevel()).relativeFilePath(e->filePath());
    for (const GitChange& c : st.changes) {
        if (c.path == rel && c.untracked()) {          // brand-new file: every line is added
            QHash<int, int> all;
            for (int n = 1; n <= e->blockCount(); ++n) all.insert(n, CodeEditor::MarkAdded);
            e->setLineMarks(all);
            return;
        }
    }
    QPointer<CodeEditor> guard(e);
    m_git->lineMarks(e->filePath(), [guard](const QHash<int, int>& marks) {
        if (guard) guard->setLineMarks(marks);
    });
}

void MainWindow::showDiffTab(const QString& key, const QString& title, const QString& text)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* dv = qobject_cast<DiffViewer*>(m_tabs->widget(i));
        if (dv && dv->property("diffKey").toString() == key) {
            dv->setDiff(text);
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    auto* dv = new DiffViewer;
    dv->setFont(SettingsDialog::editorFont());
    dv->setProperty("diffKey", key);
    dv->setDiff(text);
    const int idx = m_tabs->addTab(dv, title);
    m_tabs->setTabToolTip(idx, title);
    m_tabs->setCurrentIndex(idx);
}
