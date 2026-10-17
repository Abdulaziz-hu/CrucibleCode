#include "ThemeEditorDialog.h"

#include "ProjectTemplates.h"
#include "ThemeLibrary.h"

#include <QColorDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

ThemeEditorDialog::ThemeEditorDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Custom Theme"));
    resize(560, 700);
    auto& tm = ThemeManager::instance();
    m_previousKey = tm.currentKey();
    m_colors = tm.customColors();

    auto* root = new QVBoxLayout(this);

    auto* top = new QFormLayout;
    m_name = new QLineEdit(tm.customName());
    top->addRow(tr("Theme name:"), m_name);
    auto* baseRow = new QHBoxLayout;
    m_base = new QComboBox;
    for (const ThemeInfo& t : ThemeLibrary::builtinThemes()) m_base->addItem(t.name, t.key);
    for (const ThemeInfo& t : ThemeLibrary::installedThemes()) m_base->addItem(t.name, t.key);
    auto* baseBtn = new QPushButton(tr("Start from this"));
    baseBtn->setToolTip(tr("Replace all colours with the chosen built-in theme"));
    baseRow->addWidget(m_base, 1);
    baseRow->addWidget(baseBtn);
    top->addRow(tr("Base theme:"), baseRow);
    m_radius = new QSpinBox;
    m_radius->setRange(0, 10);
    m_radius->setSuffix(tr(" px"));
    top->addRow(tr("Corner radius:"), m_radius);
    m_fontSize = new QDoubleSpinBox;
    m_fontSize->setRange(0, 24);
    m_fontSize->setDecimals(1);
    m_fontSize->setSpecialValueText(tr("Default"));
    m_fontSize->setSuffix(tr(" pt"));
    top->addRow(tr("Interface font size:"), m_fontSize);
    root->addLayout(top);

    // one group box per colour group
    auto* inner = new QWidget;
    auto* innerLay = new QVBoxLayout(inner);
    QMap<QString, QFormLayout*> groups;
    QStringList order;
    for (const auto& f : ThemeColors::fields()) {
        const QString g = QString::fromLatin1(f.group);
        if (!groups.contains(g)) {
            auto* box = new QGroupBox(g);
            auto* form = new QFormLayout(box);
            innerLay->addWidget(box);
            groups.insert(g, form);
        }
        const QString key = QString::fromLatin1(f.key);
        auto* swatch = new QPushButton;
        swatch->setFixedSize(46, 22);
        auto* hex = new QLineEdit;
        hex->setMaximumWidth(90);
        hex->setMaxLength(9);
        auto* row = new QHBoxLayout;
        row->addWidget(swatch);
        row->addWidget(hex);
        row->addStretch(1);
        groups[g]->addRow(tr(f.label), row);
        m_swatches.insert(key, swatch);
        m_hex.insert(key, hex);
        connect(swatch, &QPushButton::clicked, this, [this, key] {
            QColor cur;
            for (const auto& fl : ThemeColors::fields())
                if (key == QLatin1String(fl.key)) cur = m_colors.*fl.member;
            const QColor c = QColorDialog::getColor(cur, this, tr("Choose colour"), QColorDialog::ShowAlphaChannel);
            if (c.isValid()) colorEdited(key, c);
        });
        connect(hex, &QLineEdit::editingFinished, this, [this, key, hex] {
            const QColor c(hex->text().trimmed());
            if (c.isValid()) colorEdited(key, c);
            else updateSwatch(key);
        });
    }
    innerLay->addStretch(1);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);
    root->addWidget(scroll, 1);

    auto* io = new QHBoxLayout;
    auto* importBtn = new QPushButton(tr("Import..."));
    auto* exportBtn = new QPushButton(tr("Export..."));
    io->addWidget(importBtn);
    io->addWidget(exportBtn);
    io->addStretch(1);
    root->addLayout(io);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(baseBtn, &QPushButton::clicked, this, [this] {
        ThemeColors c;
        if (!ThemeLibrary::load(m_base->currentData().toString(), &c)) return;
        loadColors(c);
        livePreview();
    });
    connect(m_radius, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_loading) return;
        m_colors.radius = v;
        livePreview();
    });
    connect(m_fontSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (m_loading) return;
        m_colors.uiFontSize = v;
        livePreview();
    });
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString name = m_name->text().trimmed().isEmpty() ? QStringLiteral("My Theme") : m_name->text().trimmed();
        const QString cssFilter = tr("Theme CSS file for the Theme Marketplace (*.css)");
        QString filter = cssFilter;
        const QString p = QFileDialog::getSaveFileName(this, tr("Export Theme"), ThemeLibrary::sanitizeId(name) + QStringLiteral(".css"),
                                                       cssFilter + QStringLiteral(";;") + tr("CrucibleCode theme, JSON (*.cctheme *.json)"), &filter);
        if (p.isEmpty()) return;
        QString err;
        const bool asJson = QFileInfo(p).suffix().compare(QLatin1String("css"), Qt::CaseInsensitive) != 0;
        const QString author = ProjectTemplates::author().isEmpty() ? tr("Your Name") : ProjectTemplates::author();
        const bool ok = asJson ? ThemeManager::instance().exportCustom(m_colors, name, p, &err)
                               : ThemeManager::instance().exportCss(m_colors, name, author, p, &err);
        if (!ok) QMessageBox::warning(this, tr("Export Theme"), err);
    });
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Import Theme"), QString(),
                                                       tr("CrucibleCode themes (*.css *.cctheme *.json)"));
        if (p.isEmpty()) return;
        ThemeColors c;
        QString name, err;
        if (!ThemeManager::instance().importTheme(p, &c, &name, &err)) {
            QMessageBox::warning(this, tr("Import Theme"), err);
            return;
        }
        m_name->setText(name);
        loadColors(c);
        livePreview();
    });

    loadColors(m_colors);
    livePreview();
}

void ThemeEditorDialog::loadColors(const ThemeColors& c)
{
    m_loading = true;
    m_colors = c;
    for (auto it = m_swatches.begin(); it != m_swatches.end(); ++it) updateSwatch(it.key());
    m_radius->setValue(c.radius);
    m_fontSize->setValue(c.uiFontSize);
    m_loading = false;
}

void ThemeEditorDialog::updateSwatch(const QString& key)
{
    for (const auto& f : ThemeColors::fields()) {
        if (key != QLatin1String(f.key)) continue;
        const QColor c = m_colors.*f.member;
        m_swatches[key]->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid #888; min-height: 0; padding: 0; }").arg(c.name()));
        m_hex[key]->setText(c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
    }
}

void ThemeEditorDialog::colorEdited(const QString& key, const QColor& c)
{
    for (const auto& f : ThemeColors::fields())
        if (key == QLatin1String(f.key)) m_colors.*f.member = c;
    updateSwatch(key);
    livePreview();
}

void ThemeEditorDialog::livePreview() { ThemeManager::instance().preview(m_colors); }

void ThemeEditorDialog::accept()
{
    auto& tm = ThemeManager::instance();
    tm.setCustom(m_colors, m_name->text().trimmed().isEmpty() ? QStringLiteral("My Theme") : m_name->text().trimmed());
    QSettings().setValue(QStringLiteral("theme"), QStringLiteral("custom"));
    tm.apply(ThemeManager::Custom);
    QDialog::accept();
}

void ThemeEditorDialog::reject()
{
    ThemeManager::instance().applyKey(m_previousKey);   // undo the live preview
    QDialog::reject();
}
