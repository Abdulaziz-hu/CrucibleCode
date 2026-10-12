#include "SettingsDialog.h"
#include "AiCompletion.h"
#include "AiSetupDialog.h"
#include "ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
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

bool SettingsDialog::spellCheckEnabled()
{
    return QSettings().value(QStringLiteral("editor/spellcheck"), true).toBool();
}

SettingsDialog::SettingsDialog(AiCompletion* ai, QWidget* parent) : QDialog(parent), m_aiEngine(ai)
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

    m_tabSize = new QSpinBox;
    m_tabSize->setRange(1, 16);
    m_tabSize->setValue(tabSize());
    form->addRow(tr("Tab size (spaces):"), m_tabSize);

    m_hidden = new QCheckBox(tr("Show hidden files in the Explorer (.gitignore, .env, ...)"));
    m_hidden->setChecked(showHiddenFiles());
    form->addRow(QString(), m_hidden);

    m_caret = new QComboBox;
    m_caret->addItem(tr("Solid while typing, blinks when idle"), 0);
    m_caret->addItem(tr("Always blink"), 1);
    m_caret->addItem(tr("Never blink"), 2);
    m_caret->setCurrentIndex(caretBlinkMode());
    form->addRow(tr("Cursor:"), m_caret);

    m_livePort = new QSpinBox;
    m_livePort->setRange(1024, 65000);
    m_livePort->setValue(livePort());
    form->addRow(tr("Live preview port:"), m_livePort);
    m_liveOpen = new QCheckBox(tr("Open the browser when the live preview starts (needs Python 3)"));
    m_liveOpen->setChecked(liveOpenBrowser());
    form->addRow(QString(), m_liveOpen);

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
    s.setValue(QStringLiteral("editor/tabSize"), m_tabSize->value());
    s.setValue(QStringLiteral("explorer/showHidden"), m_hidden->isChecked());
    s.setValue(QStringLiteral("editor/caretBlink"), m_caret->currentData().toInt());
    s.setValue(QStringLiteral("web/livePort"), m_livePort->value());
    s.setValue(QStringLiteral("web/openBrowser"), m_liveOpen->isChecked());
    s.setValue(QStringLiteral("editor/suggestions"), m_suggest->isChecked());
    s.setValue(QStringLiteral("editor/spellcheck"), m_spell->isChecked());
    s.setValue(QStringLiteral("ai/enabled"), m_aiBox->isChecked());
    s.setValue(QStringLiteral("cmake/path"), m_cmake->text().trimmed());
    s.setValue(QStringLiteral("cmake/generator"), m_generator->text().trimmed());
    s.setValue(QStringLiteral("cmake/extraArgs"), m_extra->text());
    const QString theme = m_theme->currentData().toString();
    s.setValue(QStringLiteral("theme"), theme);
    if (ThemeManager::nameToId(theme) != ThemeManager::instance().current())
        ThemeManager::instance().apply(ThemeManager::nameToId(theme));
    QDialog::accept();
}
