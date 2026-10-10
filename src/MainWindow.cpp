#include "MainWindow.h"
#include "CodeEditor.h"
#include "FindBar.h"
#include "QuickOpenDialog.h"
#include "SearchPanel.h"
#include "SettingsDialog.h"
#include "SyntaxHighlighter.h"
#include "TerminalPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
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
#include <QPixmap>
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
        updateMarkdownPreview();
    });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] {
        for (int i = 0; i < m_tabs->count(); ++i)
            static_cast<CodeEditor*>(m_tabs->widget(i))->applyTheme();
        if (m_themeGroup) {
            (ThemeManager::instance().isDark() ? m_actThemeDark : m_actThemeLight)->setChecked(true);
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
    applyEditorSettings();
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
    m_actZoomIn = act(tr("Zoom &In"), withEditor([](CodeEditor* e) { e->zoomIn(1); }), QKeySequence::ZoomIn);
    m_actZoomOut = act(tr("Zoom &Out"), withEditor([](CodeEditor* e) { e->zoomOut(1); }), QKeySequence::ZoomOut);
    m_actZoomReset = act(tr("&Reset Zoom"), [this] { applyEditorSettings(); }, QKeySequence(Qt::CTRL | Qt::Key_0));

    m_themeGroup = new QActionGroup(this);
    m_actThemeDark = act(tr("Dark Grey"), [this] { setTheme(ThemeManager::DarkGrey); });
    m_actThemeLight = act(tr("Light"), [this] { setTheme(ThemeManager::Light); });
    for (QAction* a : {m_actThemeDark, m_actThemeLight}) {
        a->setCheckable(true);
        m_themeGroup->addAction(a);
    }
    (ThemeManager::instance().isDark() ? m_actThemeDark : m_actThemeLight)->setChecked(true);

    m_actWordWrap = act(tr("&Word Wrap"), [this] {
        const bool on = m_actWordWrap->isChecked();
        QSettings().setValue(QStringLiteral("editor/wordWrap"), on);
        applyEditorSettings();
    });
    m_actWordWrap->setCheckable(true);
    m_actMdPreview = act(tr("Markdown &Preview"), [this] {
        m_mdDock->setVisible(!m_mdDock->isVisible());
    }, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    m_actResetLayout = act(tr("Reset Panel &Sizes"), [this] { resetPanelSizes(); });

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
    m_actStop = act(tr("&Stop"), [this] { m_runAfterBuild = false; m_build->stop(); }, QKeySequence(Qt::SHIFT | Qt::Key_F5));
    m_actRun = act(tr("&Run"), [this] { runStartup(); }, QKeySequence(Qt::Key_F5));

    // Tools / Help
    m_actSettings = act(tr("&Settings..."), [this] {
        SettingsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) applyEditorSettings();
    });
    m_actAbout = act(tr("&About CrucibleCode"), [this] { showAbout(); });
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actNew);
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
    view->addSeparator();

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
    build->addAction(m_actStop);

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(m_actSettings);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
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
    connect(m_mdDock, &QDockWidget::visibilityChanged, this, [this](bool) { updateMarkdownPreview(); });

    tabifyDockWidget(m_outputDock, m_errorDock);
    tabifyDockWidget(m_errorDock, m_searchDock);
    tabifyDockWidget(m_searchDock, m_terminalDock);
    m_outputDock->raise();

    // Default sizes: used by "double-click a splitter" and View > Reset Panel Sizes.
    m_explorerDock->setProperty("defaultSize", 260);
    m_mdDock->setProperty("defaultSize", 420);
    for (QDockWidget* d : {m_outputDock, m_errorDock, m_searchDock, m_terminalDock})
        d->setProperty("defaultSize", 220);
    resetPanelSizes();

    // View menu: dock toggles
    if (QMenu* view = menuBar()->findChild<QMenu*>(QStringLiteral("viewMenu"))) {
        view->addAction(m_explorerDock->toggleViewAction());
        view->addAction(m_outputDock->toggleViewAction());
        view->addAction(m_errorDock->toggleViewAction());
        view->addAction(m_searchDock->toggleViewAction());
        view->addAction(m_terminalDock->toggleViewAction());
        view->addAction(m_mdDock->toggleViewAction());
        view->addSeparator();
        view->addAction(m_actMdPreview);
        view->addAction(m_actResetLayout);
    }
}

void MainWindow::createStatusBar()
{
    m_posLabel = new QLabel;
    m_eolLabel = new QLabel;
    m_encLabel = new QLabel(QStringLiteral("UTF-8"));
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
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* e = static_cast<CodeEditor*>(m_tabs->widget(i));
        if (QFileInfo(e->filePath()) == QFileInfo(abs) && !e->isUntitled()) {
            m_tabs->setCurrentIndex(i);
            if (line > 0) e->goTo(line, column > 0 ? column : 1);
            return e;
        }
    }

    CodeEditor* e = newEditor();
    QString err;
    if (!e->loadFile(abs, &err)) {
        QMessageBox::warning(this, tr("Open File"), tr("Could not open %1:\n%2").arg(abs, err));
        delete e;
        return nullptr;
    }
    const int idx = m_tabs->addTab(e, e->displayName());
    watchEditor(e);
    m_tabs->setTabToolTip(idx, abs);
    m_tabs->setCurrentIndex(idx);
    if (line > 0) e->goTo(line, column > 0 ? column : 1);
    else e->setFocus();
    return e;
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
    QString err;
    if (!e->saveFile(path, &err)) {
        QMessageBox::warning(this, tr("Save File"), tr("Could not save %1:\n%2").arg(path, err));
        return false;
    }
    updateTabTitle(e);
    watchEditor(e);
    const int idx = m_tabs->indexOf(e);
    if (idx >= 0) m_tabs->setTabToolTip(idx, e->filePath());
    updateWindowTitle();
    statusBar()->showMessage(tr("Saved %1").arg(e->displayName()), 3000);
    return true;
}

void MainWindow::saveAllModified()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* e = static_cast<CodeEditor*>(m_tabs->widget(i));
        if (e->document()->isModified() && !e->isUntitled()) saveEditor(e);
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
    auto* e = qobject_cast<CodeEditor*>(m_tabs->widget(index));
    if (!e || !maybeSave(e)) return;
    unwatchEditor(e);
    m_tabs->removeTab(index);
    e->deleteLater();
    updateStatus();
    updateWindowTitle();
}

void MainWindow::closeTabForPath(const QString& path)
{
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        auto* e = static_cast<CodeEditor*>(m_tabs->widget(i));
        if (!e->isUntitled() && QFileInfo(e->filePath()) == QFileInfo(path)) {
            m_tabs->removeTab(i);
            e->deleteLater();
        }
    }
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
        auto* e = static_cast<CodeEditor*>(m_tabs->widget(i));
        e->setEditorFont(f);
        e->setTabSize(SettingsDialog::tabSize());
        e->setWordWrap(wrap);
    }
    m_output->setFont(f);
    m_terminals->setTerminalFont(f);
    applyExplorerFilter();
    m_build->setCMakePath(s.value(QStringLiteral("cmake/path")).toString());
    m_build->setGenerator(s.value(QStringLiteral("cmake/generator")).toString());
    m_build->setExtraArguments(s.value(QStringLiteral("cmake/extraArgs")).toString());
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
        m_output->clear();
        m_errors->clear();
        m_errorDock->setWindowTitle(tr("Error List"));
        m_findBar->hide();
    }
    m_root = newRoot;

    const QModelIndex rootIdx = m_fs->setRootPath(m_root);
    m_tree->setRootIndex(rootIdx);
    m_explorerDock->setWindowTitle(tr("Explorer - %1").arg(QFileInfo(m_root).fileName()));

    m_build->setProjectRoot(m_root);
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
        auto* e = static_cast<CodeEditor*>(m_tabs->widget(i));
        m_tabs->setCurrentIndex(i);
        if (!maybeSave(e)) return false;
        unwatchEditor(e);
        m_tabs->removeTab(i);
        e->deleteLater();
    }
    updateStatus();
    updateWindowTitle();
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
        auto* e = static_cast<CodeEditor*>(m_tabs->widget(i));
        if (e->isUntitled()) continue;
        const QTextCursor c = e->textCursor();
        files << QStringLiteral("%1|%2|%3").arg(e->filePath()).arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1);
        if (i == m_tabs->currentIndex()) current = e->filePath();
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
        if (static_cast<CodeEditor*>(m_tabs->widget(i))->filePath() == current) {
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

    if (chosen == aNewFile) {
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
        auto* ed = static_cast<CodeEditor*>(m_tabs->widget(i));
        m_tabs->setCurrentIndex(i);
        if (!maybeSave(ed)) {
            e->ignore();
            return;
        }
    }
    m_build->stop();
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
        : tr("Install KSyntaxHighlighting and rebuild to add 300+ more languages of highlighting.");

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
        "<p>Integrated native terminals (PowerShell, cmd, bash, zsh, ...), Markdown preview, find in files.</p>")
        .arg(QApplication::applicationVersion(), builtin, more));
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
        auto* e = static_cast<CodeEditor*>(m_tabs->widget(i));
        if (!e->isUntitled() && QFileInfo(e->filePath()) == QFileInfo(path)) { editor = e; break; }
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
    QAction* aCopy = menu.addAction(tr("Copy Path"));
    aCopy->setEnabled(!e->isUntitled());
    QAction* aReveal = menu.addAction(tr("Open Containing Folder"));
    aReveal->setEnabled(!e->isUntitled());

    QAction* chosen = menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aSave) saveEditor(e);
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
