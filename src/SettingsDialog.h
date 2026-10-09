#pragma once
#include <QDialog>
#include <QFont>

class QComboBox;
class QFontComboBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Editor font from stored settings (falls back to a platform monospace font).
    static QFont editorFont();

private:
    void accept() override;

    QComboBox* m_theme = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QLineEdit* m_cmake = nullptr;
    QLineEdit* m_generator = nullptr;
    QLineEdit* m_extra = nullptr;
};
