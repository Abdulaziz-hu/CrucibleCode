#pragma once
#include <QDialog>
#include <QFont>

class AiCompletion;
class QCheckBox;
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
};
