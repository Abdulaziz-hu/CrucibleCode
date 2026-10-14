#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

struct TemplateFile {
    QString path;          // relative, '/' separators, may contain {{placeholders}}
    QByteArray data;       // raw bytes (text files are UTF-8 and get placeholders replaced)
    bool text = true;
    bool executable = false;
};

struct ProjectTemplate {
    QString id;            // "builtin:cpp-console" or a file-name slug for user templates
    QString name;
    QString description;
    QList<TemplateFile> files;
    bool builtin = false;
    bool isValid() const { return !id.isEmpty(); }
};

// Project templates: built-in ones plus user templates stored as *.cctemplate (JSON) files that can be
// exported / imported to share them. Placeholders replaced when a project is created:
//   {{name}} {{NAME}} {{name_lower}} {{identifier}} {{year}} {{date}} {{author}}
namespace ProjectTemplates {

QString userDirectory();
QList<ProjectTemplate> all();
ProjectTemplate byId(const QString& id);

ProjectTemplate fromFolder(const QString& dir, const QString& name, const QString& description,
                           bool replaceFolderName, QString* error = nullptr);
bool save(const ProjectTemplate& t, QString* error = nullptr);      // into the user directory
bool remove(const QString& id);
bool exportTo(const ProjectTemplate& t, const QString& path, QString* error = nullptr);
bool importFrom(const QString& path, ProjectTemplate* out, QString* error = nullptr);   // also saves it

// Creates <parentDir>/<projectName> from the template. Returns the new folder or an empty string.
QString create(const ProjectTemplate& t, const QString& parentDir, const QString& projectName, QString* error = nullptr);

// Settings
QString defaultId();                  // "builtin:cpp-console" unless changed
void setDefaultId(const QString& id);
bool alwaysUseDefault();              // skip the template chooser in the New Project dialog
void setAlwaysUseDefault(bool on);
QString author();
void setAuthor(const QString& a);

}
