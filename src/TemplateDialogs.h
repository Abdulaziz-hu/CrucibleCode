#pragma once
#include "ProjectTemplates.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;

// File > New Project...
class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(const QString& startDir, QWidget* parent = nullptr);
    QString createdPath() const { return m_created; }

private:
    void accept() override;
    void updateDescription();

    QLineEdit* m_name = nullptr;
    QLineEdit* m_location = nullptr;
    QComboBox* m_template = nullptr;
    QLabel* m_desc = nullptr;
    QString m_created;
    QList<ProjectTemplate> m_templates;
    bool m_fixed = false;       // "always use the default template" is on
};

// Tools > Project Templates...: create from a folder, import, export, delete, choose the default.
class TemplateManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit TemplateManagerDialog(const QString& currentProject, QWidget* parent = nullptr);

private:
    void reload(const QString& select = QString());
    void showCurrent();
    ProjectTemplate current() const;

    QListWidget* m_list = nullptr;
    QLabel* m_desc = nullptr;
    QTreeWidget* m_files = nullptr;
    QCheckBox* m_always = nullptr;
    QLabel* m_defaultLabel = nullptr;
    QString m_project;
    QList<ProjectTemplate> m_templates;
};
