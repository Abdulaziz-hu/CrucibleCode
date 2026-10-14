#include "TemplateDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------------------------
//  New project
// ---------------------------------------------------------------------------------------------

NewProjectDialog::NewProjectDialog(const QString& startDir, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("New Project"));
    setMinimumWidth(480);
    m_templates = ProjectTemplates::all();
    m_fixed = ProjectTemplates::alwaysUseDefault();

    auto* form = new QFormLayout;
    m_name = new QLineEdit(QStringLiteral("MyProject"));
    form->addRow(tr("Project name:"), m_name);

    m_location = new QLineEdit(startDir.isEmpty()
        ? QSettings().value(QStringLiteral("templates/lastLocation"),
                            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString()
        : startDir);
    auto* browse = new QPushButton(tr("Browse..."));
    auto* locRow = new QHBoxLayout;
    locRow->addWidget(m_location, 1);
    locRow->addWidget(browse);
    form->addRow(tr("Location:"), locRow);

    m_template = new QComboBox;
    const QString def = ProjectTemplates::defaultId();
    int defIndex = 0;
    for (int i = 0; i < m_templates.size(); ++i) {
        m_template->addItem(m_templates[i].name + (m_templates[i].id == def ? tr("  (default)") : QString()), m_templates[i].id);
        if (m_templates[i].id == def) defIndex = i;
    }
    m_template->setCurrentIndex(defIndex);
    m_desc = new QLabel;
    m_desc->setWordWrap(true);
    if (m_fixed) {
        form->addRow(tr("Template:"), new QLabel(m_templates.value(defIndex).name));
        m_template->hide();
        form->addRow(QString(), new QLabel(tr("<i>The default template is always used (change this in Settings, Snippets & Templates).</i>")));
    } else {
        form->addRow(tr("Template:"), m_template);
    }
    form->addRow(QString(), m_desc);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Project location"), m_location->text());
        if (!d.isEmpty()) m_location->setText(d);
    });
    connect(m_template, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateDescription(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateDescription();
    m_name->selectAll();
}

void NewProjectDialog::updateDescription()
{
    const int i = m_fixed ? int(m_templates.size()) : m_template->currentIndex();
    const QString id = m_fixed ? ProjectTemplates::defaultId() : m_template->currentData().toString();
    Q_UNUSED(i)
    for (const ProjectTemplate& t : m_templates)
        if (t.id == id) m_desc->setText(tr("%1 (%n file(s))", nullptr, int(t.files.size())).arg(t.description));
}

void NewProjectDialog::accept()
{
    QString id = m_fixed ? ProjectTemplates::defaultId() : m_template->currentData().toString();
    ProjectTemplate t = ProjectTemplates::byId(id);
    if (!t.isValid()) t = ProjectTemplates::byId(QStringLiteral("builtin:empty"));
    QString err;
    const QString path = ProjectTemplates::create(t, m_location->text().trimmed(), m_name->text(), &err);
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("New Project"), err);
        return;
    }
    QSettings().setValue(QStringLiteral("templates/lastLocation"), m_location->text().trimmed());
    m_created = path;
    QDialog::accept();
}

// ---------------------------------------------------------------------------------------------
//  Template manager
// ---------------------------------------------------------------------------------------------

TemplateManagerDialog::TemplateManagerDialog(const QString& currentProject, QWidget* parent)
    : QDialog(parent), m_project(currentProject)
{
    setWindowTitle(tr("Project Templates"));
    resize(720, 460);

    m_list = new QListWidget;
    m_desc = new QLabel;
    m_desc->setWordWrap(true);
    m_files = new QTreeWidget;
    m_files->setHeaderLabels({tr("Files in the template")});
    m_defaultLabel = new QLabel;

    auto* right = new QVBoxLayout;
    right->addWidget(m_desc);
    right->addWidget(m_files, 1);
    right->addWidget(m_defaultLabel);

    auto* body = new QHBoxLayout;
    body->addWidget(m_list, 2);
    body->addLayout(right, 3);

    auto* fromFolder = new QPushButton(m_project.isEmpty() ? tr("New from Folder...") : tr("New from Current Project..."));
    auto* fromOther = new QPushButton(tr("From Folder..."));
    auto* imp = new QPushButton(tr("Import..."));
    auto* exp = new QPushButton(tr("Export..."));
    auto* del = new QPushButton(tr("Delete"));
    auto* makeDefault = new QPushButton(tr("Set as Default"));
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(fromFolder);
    if (!m_project.isEmpty()) btnRow->addWidget(fromOther);
    btnRow->addWidget(imp);
    btnRow->addWidget(exp);
    btnRow->addWidget(del);
    btnRow->addStretch(1);
    btnRow->addWidget(makeDefault);

    m_always = new QCheckBox(tr("Always use the default template when creating a project (do not ask)"));
    m_always->setChecked(ProjectTemplates::alwaysUseDefault());
    connect(m_always, &QCheckBox::toggled, this, [](bool on) { ProjectTemplates::setAlwaysUseDefault(on); });

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* hint = new QLabel(tr("Placeholders in files and file names: <tt>{{name}} {{NAME}} {{name_lower}} {{identifier}} {{year}} {{date}} {{author}}</tt>"));
    hint->setWordWrap(true);

    auto* root = new QVBoxLayout(this);
    root->addLayout(body, 1);
    root->addLayout(btnRow);
    root->addWidget(m_always);
    root->addWidget(hint);
    root->addWidget(close);

    const auto createFrom = [this](const QString& dir) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Template"), tr("Template name:"), QLineEdit::Normal,
                                                   QFileInfo(dir).fileName(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        const QString desc = QInputDialog::getText(this, tr("New Template"), tr("Description (optional):"), QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        const bool replace = QMessageBox::question(this, tr("New Template"),
            tr("Replace the folder name \"%1\" inside files and file names with {{name}}?").arg(QFileInfo(dir).fileName()))
            == QMessageBox::Yes;
        QString err;
        ProjectTemplate t = ProjectTemplates::fromFolder(dir, name, desc, replace, &err);
        if (!t.isValid() || !ProjectTemplates::save(t, &err)) {
            QMessageBox::warning(this, tr("New Template"), err);
            return;
        }
        reload(name);
    };
    connect(fromFolder, &QPushButton::clicked, this, [this, createFrom] {
        const QString dir = m_project.isEmpty() ? QFileDialog::getExistingDirectory(this, tr("Folder to turn into a template")) : m_project;
        if (!dir.isEmpty()) createFrom(dir);
    });
    connect(fromOther, &QPushButton::clicked, this, [this, createFrom] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder to turn into a template"), m_project);
        if (!dir.isEmpty()) createFrom(dir);
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Template"), QString(),
                                                                tr("CrucibleCode templates (*.cctemplate *.json)"));
        QString last;
        for (const QString& f : files) {
            QString err;
            ProjectTemplate t;
            if (!ProjectTemplates::importFrom(f, &t, &err)) QMessageBox::warning(this, tr("Import Template"), err);
            else last = t.name;
        }
        if (!last.isEmpty()) reload(last);
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const ProjectTemplate t = current();
        if (!t.isValid()) return;
        const QString p = QFileDialog::getSaveFileName(this, tr("Export Template"), t.name + QStringLiteral(".cctemplate"),
                                                       tr("CrucibleCode templates (*.cctemplate)"));
        QString err;
        if (!p.isEmpty() && !ProjectTemplates::exportTo(t, p, &err)) QMessageBox::warning(this, tr("Export Template"), err);
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const ProjectTemplate t = current();
        if (!t.isValid() || t.builtin) {
            QMessageBox::information(this, tr("Delete"), tr("Built-in templates cannot be deleted."));
            return;
        }
        if (QMessageBox::question(this, tr("Delete Template"), tr("Delete the template \"%1\"?").arg(t.name)) == QMessageBox::Yes) {
            ProjectTemplates::remove(t.id);
            reload();
        }
    });
    connect(makeDefault, &QPushButton::clicked, this, [this] {
        const ProjectTemplate t = current();
        if (t.isValid()) { ProjectTemplates::setDefaultId(t.id); reload(t.name); }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { showCurrent(); });
    reload();
}

ProjectTemplate TemplateManagerDialog::current() const
{
    const int r = m_list->currentRow();
    return (r >= 0 && r < m_templates.size()) ? m_templates.at(r) : ProjectTemplate();
}

void TemplateManagerDialog::reload(const QString& select)
{
    m_templates = ProjectTemplates::all();
    m_list->clear();
    const QString def = ProjectTemplates::defaultId();
    int sel = 0;
    for (int i = 0; i < m_templates.size(); ++i) {
        const ProjectTemplate& t = m_templates[i];
        QString label = t.name;
        if (t.builtin) label += tr("  [built-in]");
        if (t.id == def) label += tr("  [default]");
        m_list->addItem(label);
        if (!select.isEmpty() && t.name == select) sel = i;
    }
    m_list->setCurrentRow(sel);
    showCurrent();
}

void TemplateManagerDialog::showCurrent()
{
    const ProjectTemplate t = current();
    m_files->clear();
    m_desc->setText(t.isValid() ? QStringLiteral("<b>%1</b><br>%2").arg(t.name.toHtmlEscaped(), t.description.toHtmlEscaped()) : QString());
    for (const TemplateFile& f : t.files) m_files->addTopLevelItem(new QTreeWidgetItem({f.path}));
    const ProjectTemplate d = ProjectTemplates::byId(ProjectTemplates::defaultId());
    m_defaultLabel->setText(tr("Default template: <b>%1</b>").arg(d.name.toHtmlEscaped()));
}
