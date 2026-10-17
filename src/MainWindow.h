#pragma once
#include "BuildManager.h"
#include "ThemeManager.h"

#include <QMainWindow>

class AiCompletion;
class LspManager;
class PerfMonitor;
class Updater;
struct ReleaseInfo;
class GitManager;
class GitPanel;
class LiveServer;
class CodeEditor;
class QToolButton;
class FindBar;
class SearchPanel;
class QAction;
class QActionGroup;
class QComboBox;
class QDockWidget;
class QFileSystemModel;
class GitExplorerDelegate;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QTabWidget;
class QTextBrowser;
class QTimer;
class TerminalPanel;
class QTreeView;
class QTreeWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFolder(const QString& path);
    CodeEditor* openFile(const QString& path, int line = -1, int column = -1);
    void restoreLastSession();

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    // construction
    void createActions();
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();

    // documents
    CodeEditor* currentEditor() const;
    CodeEditor* newEditor();
    void newFile();
    bool saveEditor(CodeEditor* e, bool saveAs = false);
    void saveAllModified();
    bool maybeSave(CodeEditor* e);
    void closeTab(int index);
    void closeTabForPath(const QString& path);
    void updateTabTitle(CodeEditor* e);

    // ui state
    void updateWindowTitle();
    void updateStatus();
    void updateBuildActions();
    void updateRecentMenu();
    void refreshStartupItems();
    void applyEditorSettings();
    void setTheme(ThemeManager::Id id);
    void setThemeKey(const QString& key);
    void updateThemeActions();
    void newProject();
    void checkForUpdates(bool manual);
    void showUpdateDialog(const ReleaseInfo& release);
    void applyUpdate(const QString& file);
    bool saveOrDiscardAll();
    void applySaveTransforms(CodeEditor* e);
    void registerShortcuts();
    void refreshProblems();
    void updateLspUi();
    void insertSnippetDialog();

    // build
    void appendOutput(const QString& text, BuildManager::Kind kind);
    void addDiagnostic(const Diagnostic& d);
    void onBuildStarted(const QString& title);
    void onBuildFinished(int code, bool ok);
    void runStartup();

    // tabs that are not text editors (images, PDFs, other files)
    CodeEditor* editorAt(int index) const;
    QString tabFilePath(int index) const;
    int tabIndexForPath(const QString& path) const;
    void addViewerTab(QWidget* viewer, const QString& path);
    void showExternal(const QString& path, const QString& note, bool offerText);
    CodeEditor* openTextEditor(const QString& path, int line, int column);
    void openAsText(const QString& path);
    void runFile(const QString& path);
    void updateMarkdownButton();
    void updateAiLabel();
    void updateLiveUi();
    void updateGitUi();
    void refreshGitMarks(CodeEditor* e);
    void showDiffTab(const QString& key, const QString& title, const QString& text);
    void toggleLive();
    void openLiveInBrowser();

    // sessions / panels / misc
    bool closeAllEditors();
    static QString sessionKey(const QString& root);
    void saveSession();
    void restoreSession();
    bool resetDockAt(const QPoint& pos);
    void resetDockSize(QDockWidget* dock);
    void resetPanelSizes();
    void updateMarkdownPreview();
    void showAbout();
    void quickOpen();
    void applyExplorerFilter();
    void watchEditor(CodeEditor* e);
    void unwatchEditor(CodeEditor* e);
    void onExternalChange(const QString& path);
    void showTabContextMenu(const QPoint& pos);

    // project tree
    void showTreeContextMenu(const QPoint& pos);

    // widgets
    QTabWidget* m_tabs = nullptr;
    FindBar* m_findBar = nullptr;
    QTreeView* m_tree = nullptr;
    QFileSystemModel* m_fs = nullptr;
    GitExplorerDelegate* m_gitDelegate = nullptr;
    QPlainTextEdit* m_output = nullptr;
    QTreeWidget* m_errors = nullptr;
    SearchPanel* m_search = nullptr;
    QDockWidget* m_explorerDock = nullptr;
    QDockWidget* m_outputDock = nullptr;
    QDockWidget* m_errorDock = nullptr;
    QDockWidget* m_searchDock = nullptr;
    QComboBox* m_configCombo = nullptr;
    QComboBox* m_startupCombo = nullptr;
    QLabel* m_posLabel = nullptr;
    QLabel* m_eolLabel = nullptr;
    QLabel* m_encLabel = nullptr;
    QMenu* m_recentMenu = nullptr;
    QActionGroup* m_themeGroup = nullptr;
    QMenu* m_moreThemes = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    AiCompletion* m_ai = nullptr;
    QLabel* m_aiLabel = nullptr;
    QToolButton* m_mdButton = nullptr;
    LiveServer* m_live = nullptr;
    GitManager* m_git = nullptr;
    GitPanel* m_gitPanel = nullptr;
    QDockWidget* m_gitDock = nullptr;
    QLabel* m_gitLabel = nullptr;
    QAction* m_actSourceControl = nullptr;
    QToolButton* m_liveButton = nullptr;
    QToolButton* m_liveOpenButton = nullptr;
    QLabel* m_liveLabel = nullptr;
    QAction *m_actLiveToggle = nullptr, *m_actLiveOpen = nullptr, *m_actBrowserFile = nullptr;
    QAction* m_actRunFile = nullptr;
    QAction* m_actQuickOpen = nullptr;
    TerminalPanel* m_terminals = nullptr;
    QDockWidget* m_terminalDock = nullptr;
    QDockWidget* m_mdDock = nullptr;
    QTextBrowser* m_mdView = nullptr;
    QTimer* m_mdTimer = nullptr;
    QAction *m_actNewTerminal = nullptr, *m_actToggleTerminal = nullptr, *m_actCloseTerminal = nullptr,
            *m_actCloseAllTerminals = nullptr, *m_actWordWrap = nullptr, *m_actMdPreview = nullptr,
            *m_actResetLayout = nullptr;

    // actions
    QAction *m_actNew = nullptr, *m_actOpenFile = nullptr, *m_actOpenFolder = nullptr,
            *m_actSave = nullptr, *m_actSaveAs = nullptr, *m_actSaveAll = nullptr,
            *m_actClose = nullptr, *m_actExit = nullptr;
    QAction *m_actUndo = nullptr, *m_actRedo = nullptr, *m_actCut = nullptr, *m_actCopy = nullptr,
            *m_actPaste = nullptr, *m_actSelectAll = nullptr, *m_actFind = nullptr,
            *m_actReplace = nullptr, *m_actFindInFiles = nullptr, *m_actGoTo = nullptr,
            *m_actComment = nullptr;
    QAction *m_actConfigure = nullptr, *m_actBuild = nullptr, *m_actRebuild = nullptr,
            *m_actClean = nullptr, *m_actStop = nullptr, *m_actRun = nullptr;
    QAction *m_actSettings = nullptr, *m_actAbout = nullptr, *m_actZoomIn = nullptr,
            *m_actZoomOut = nullptr, *m_actZoomReset = nullptr, *m_actThemeDark = nullptr,
            *m_actThemeLight = nullptr;

    LspManager* m_lsp = nullptr;
    PerfMonitor* m_perf = nullptr;
    Updater* m_updater = nullptr;
    QLabel* m_perfLabel = nullptr;
    QLabel* m_lspLabel = nullptr;
    QTreeWidget* m_problems = nullptr;
    QDockWidget* m_problemsDock = nullptr;
    bool m_updateCheckRunning = false;
    QAction *m_actThemeCustom = nullptr, *m_actEditTheme = nullptr, *m_actThemeMarket = nullptr, *m_actNewProject = nullptr, *m_actTemplates = nullptr,
            *m_actSnippets = nullptr, *m_actInsertSnippet = nullptr, *m_actGotoDef = nullptr, *m_actHover = nullptr,
            *m_actCheckUpdates = nullptr, *m_actPerf = nullptr;

    BuildManager* m_build = nullptr;
    QString m_root;
    int m_untitledCounter = 0;
    bool m_runAfterBuild = false;
};
