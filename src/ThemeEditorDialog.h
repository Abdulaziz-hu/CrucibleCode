#pragma once
#include "ThemeManager.h"

#include <QDialog>
#include <QMap>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

// Editor for the "Custom Theme": every colour of the UI, editor and syntax highlighting, applied
// live while editing. Themes can be exported to / imported from a .cctheme file to share them.
class ThemeEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditorDialog(QWidget* parent = nullptr);

private:
    void accept() override;
    void reject() override;
    void loadColors(const ThemeColors& c);
    void colorEdited(const QString& key, const QColor& c);
    void updateSwatch(const QString& key);
    void livePreview();

    ThemeColors m_colors;
    ThemeManager::Id m_previousId;
    QLineEdit* m_name = nullptr;
    QComboBox* m_base = nullptr;
    QSpinBox* m_radius = nullptr;
    QDoubleSpinBox* m_fontSize = nullptr;
    QMap<QString, QPushButton*> m_swatches;
    QMap<QString, QLineEdit*> m_hex;
    bool m_loading = false;
};
