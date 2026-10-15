#pragma once
#include <QDialog>
#include <QFont>
#include <QList>
#include "LspManager.h"

class AiCompletion;
class ShortcutsEditor;
class QCheckBox;
class QTableWidget;
class QLabel;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(AiCompletion* ai, QWidget* parent = nullptr);

    // Editor font from stored settings (falls back to a platform monospace font).
    static QFont editorFont();
    static bool showHiddenFiles();   // Explorer: dot-files etc. (default: shown)
    static int tabSize();            // spaces per tab (default 4)
    static bool suggestionsEnabled();    // code suggestion popup (default off)
    static bool spellCheckEnabled();     // Markdown / text spell check (default on)
    static int caretBlinkMode();         // 0 solid while typing, 1 always blink, 2 never
    static int livePort();               // live preview server: first port to try (default 5500)
    static bool liveOpenBrowser();       // open the browser when the live server starts (default on)
    static bool gitDecorations();        // M / U / A / D markers in the Explorer and tabs (default on)
    static bool perfMonitorEnabled();    // CPU / RAM / threads in the status bar (default off)
    static bool trimTrailingWhitespace();   // on save (default off)
    static bool ensureFinalNewline();       // on save (default off)

signals:
    void checkForUpdatesRequested();

private:
    void accept() override;

    QComboBox* m_theme = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QSpinBox* m_tabSize = nullptr;
    QCheckBox* m_hidden = nullptr;
    QCheckBox* m_suggest = nullptr;
    QCheckBox* m_spell = nullptr;
    QComboBox* m_caret = nullptr;
    QSpinBox* m_livePort = nullptr;
    QCheckBox* m_liveOpen = nullptr;
    QCheckBox* m_aiBox = nullptr;
    AiCompletion* m_aiEngine = nullptr;
    QLineEdit* m_cmake = nullptr;
    QLineEdit* m_generator = nullptr;
    QLineEdit* m_extra = nullptr;
    QCheckBox* m_perf = nullptr;
    QCheckBox* m_gitDeco = nullptr;
    QCheckBox* m_trim = nullptr;
    QCheckBox* m_finalNewline = nullptr;
    ShortcutsEditor* m_shortcuts = nullptr;
    QComboBox* m_defaultTemplate = nullptr;
    QCheckBox* m_alwaysDefault = nullptr;
    QLineEdit* m_author = nullptr;
    QCheckBox* m_lspEnabled = nullptr;
    QTableWidget* m_lspTable = nullptr;
    QCheckBox* m_autoUpdate = nullptr;
    QCheckBox* m_prerelease = nullptr;
    QString m_originalTheme;

    void fillLspTable(const QList<LspServerConfig>& list);
    void editCustomTheme();
};
