#include "SettingsDialog.h"
#include "ThemeManager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
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

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setMinimumWidth(460);

    QSettings s;
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_theme = new QComboBox;
    m_theme->addItem(tr("Dark Grey"), QStringLiteral("dark"));
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->setCurrentIndex(ThemeManager::instance().current() == ThemeManager::Light ? 1 : 0);
    form->addRow(tr("Theme:"), m_theme);

    m_font = new QFontComboBox;
    m_font->setFontFilters(QFontComboBox::MonospacedFonts);
    m_font->setCurrentFont(editorFont());
    form->addRow(tr("Editor font:"), m_font);

    m_fontSize = new QSpinBox;
    m_fontSize->setRange(6, 40);
    m_fontSize->setValue(editorFont().pointSize());
    form->addRow(tr("Font size:"), m_fontSize);

    m_cmake = new QLineEdit(s.value(QStringLiteral("cmake/path")).toString());
    m_cmake->setPlaceholderText(tr("cmake (found on PATH)"));
    form->addRow(tr("CMake executable:"), m_cmake);

    m_generator = new QLineEdit(s.value(QStringLiteral("cmake/generator")).toString());
    m_generator->setPlaceholderText(tr("Default (e.g. Ninja)"));
    form->addRow(tr("CMake generator:"), m_generator);

    m_extra = new QLineEdit(s.value(QStringLiteral("cmake/extraArgs")).toString());
    m_extra->setPlaceholderText(tr("-DCMAKE_PREFIX_PATH=/path/to/Qt ..."));
    form->addRow(tr("Extra configure args:"), m_extra);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
}

void SettingsDialog::accept()
{
    QSettings s;
    s.setValue(QStringLiteral("editor/fontFamily"), m_font->currentFont().family());
    s.setValue(QStringLiteral("editor/fontSize"), m_fontSize->value());
    s.setValue(QStringLiteral("cmake/path"), m_cmake->text().trimmed());
    s.setValue(QStringLiteral("cmake/generator"), m_generator->text().trimmed());
    s.setValue(QStringLiteral("cmake/extraArgs"), m_extra->text());
    const QString theme = m_theme->currentData().toString();
    s.setValue(QStringLiteral("theme"), theme);
    ThemeManager::instance().apply(ThemeManager::nameToId(theme));
    QDialog::accept();
}
