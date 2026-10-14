#include "ProjectTemplates.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace ProjectTemplates {

static QString tr(const char* s) { return QCoreApplication::translate("ProjectTemplates", s); }

// ---------------------------------------------------------------------------------------------
//  Built-in templates
// ---------------------------------------------------------------------------------------------

static TemplateFile tf(const QString& path, const QString& text, bool exec = false)
{
    TemplateFile f;
    f.path = path;
    f.data = text.toUtf8();
    f.text = true;
    f.executable = exec;
    return f;
}

static ProjectTemplate makeBuiltin(const QString& slug, const QString& name, const QString& desc, QList<TemplateFile> files)
{
    ProjectTemplate t;
    t.id = QStringLiteral("builtin:") + slug;
    t.name = name;
    t.description = desc;
    t.files = std::move(files);
    t.builtin = true;
    return t;
}

static const QString kGitignoreCpp = QStringLiteral("build/\n.cache/\n*.o\n*.obj\n*.exe\n.vs/\n");

static QList<ProjectTemplate> builtins()
{
    QList<ProjectTemplate> l;

    l << makeBuiltin(QStringLiteral("empty"), tr("Empty project"), tr("An empty folder with a README."),
        {tf(QStringLiteral("README.md"), QStringLiteral("# {{name}}\n\nCreated {{date}} by {{author}}.\n"))});

    l << makeBuiltin(QStringLiteral("cpp-console"), tr("C++ console application (CMake)"),
        tr("CMake project with a C++20 main.cpp."),
        {tf(QStringLiteral("CMakeLists.txt"), QStringLiteral(
            "cmake_minimum_required(VERSION 3.21)\n"
            "project({{identifier}} VERSION 0.1.0 LANGUAGES CXX)\n\n"
            "set(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n\n"
            "add_executable({{identifier}} src/main.cpp)\n")),
         tf(QStringLiteral("src/main.cpp"), QStringLiteral(
            "#include <iostream>\n\nint main()\n{\n    std::cout << \"Hello from {{name}}!\\n\";\n    return 0;\n}\n")),
         tf(QStringLiteral(".gitignore"), kGitignoreCpp),
         tf(QStringLiteral("README.md"), QStringLiteral("# {{name}}\n\nA C++ project. Build with Ctrl+Shift+B, run with F5.\n"))});

    l << makeBuiltin(QStringLiteral("c-console"), tr("C console application (CMake)"),
        tr("CMake project with a C17 main.c."),
        {tf(QStringLiteral("CMakeLists.txt"), QStringLiteral(
            "cmake_minimum_required(VERSION 3.21)\n"
            "project({{identifier}} VERSION 0.1.0 LANGUAGES C)\n\n"
            "set(CMAKE_C_STANDARD 17)\nset(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n\n"
            "add_executable({{identifier}} src/main.c)\n")),
         tf(QStringLiteral("src/main.c"), QStringLiteral(
            "#include <stdio.h>\n\nint main(void)\n{\n    printf(\"Hello from {{name}}!\\n\");\n    return 0;\n}\n")),
         tf(QStringLiteral(".gitignore"), kGitignoreCpp)});

    l << makeBuiltin(QStringLiteral("qt-widgets"), tr("Qt 6 Widgets application (CMake)"),
        tr("A window with a label, ready to extend."),
        {tf(QStringLiteral("CMakeLists.txt"), QStringLiteral(
            "cmake_minimum_required(VERSION 3.21)\n"
            "project({{identifier}} VERSION 0.1.0 LANGUAGES CXX)\n\n"
            "set(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_AUTOMOC ON)\n"
            "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n\n"
            "find_package(Qt6 REQUIRED COMPONENTS Widgets)\n"
            "qt_add_executable({{identifier}} WIN32 src/main.cpp)\n"
            "target_link_libraries({{identifier}} PRIVATE Qt6::Widgets)\n")),
         tf(QStringLiteral("src/main.cpp"), QStringLiteral(
            "#include <QApplication>\n#include <QLabel>\n\nint main(int argc, char* argv[])\n{\n"
            "    QApplication app(argc, argv);\n    QLabel label(QStringLiteral(\"Hello from {{name}}!\"));\n"
            "    label.resize(320, 120);\n    label.setAlignment(Qt::AlignCenter);\n    label.show();\n    return app.exec();\n}\n")),
         tf(QStringLiteral(".gitignore"), kGitignoreCpp)});

    l << makeBuiltin(QStringLiteral("csharp-console"), tr("C# console application (.NET)"),
        tr("A .NET 8 console project (needs the dotnet SDK)."),
        {tf(QStringLiteral("{{name}}.csproj"), QStringLiteral(
            "<Project Sdk=\"Microsoft.NET.Sdk\">\n  <PropertyGroup>\n    <OutputType>Exe</OutputType>\n"
            "    <TargetFramework>net8.0</TargetFramework>\n    <Nullable>enable</Nullable>\n"
            "    <ImplicitUsings>enable</ImplicitUsings>\n  </PropertyGroup>\n</Project>\n")),
         tf(QStringLiteral("Program.cs"), QStringLiteral("Console.WriteLine(\"Hello from {{name}}!\");\n")),
         tf(QStringLiteral(".gitignore"), QStringLiteral("bin/\nobj/\n.vs/\n"))});

    l << makeBuiltin(QStringLiteral("web"), tr("Static website (HTML / CSS / JS)"),
        tr("index.html with a stylesheet and a script; use Go Live to preview."),
        {tf(QStringLiteral("index.html"), QStringLiteral(
            "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\">\n"
            "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "  <title>{{name}}</title>\n  <link rel=\"stylesheet\" href=\"style.css\">\n</head>\n<body>\n"
            "  <h1>{{name}}</h1>\n  <script src=\"script.js\"></script>\n</body>\n</html>\n")),
         tf(QStringLiteral("style.css"), QStringLiteral("body {\n  font-family: system-ui, sans-serif;\n  margin: 2rem;\n}\n")),
         tf(QStringLiteral("script.js"), QStringLiteral("console.log(\"{{name}} loaded\");\n"))});
    return l;
}

// ---------------------------------------------------------------------------------------------
//  Storage
// ---------------------------------------------------------------------------------------------

QString userDirectory()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/templates");
    QDir().mkpath(d);
    return d;
}

static QJsonObject toJson(const ProjectTemplate& t)
{
    QJsonArray files;
    for (const TemplateFile& f : t.files) {
        QJsonObject o{{QStringLiteral("path"), f.path}};
        if (f.text) o.insert(QStringLiteral("content"), QString::fromUtf8(f.data));
        else {
            o.insert(QStringLiteral("encoding"), QStringLiteral("base64"));
            o.insert(QStringLiteral("content"), QString::fromLatin1(f.data.toBase64()));
        }
        if (f.executable) o.insert(QStringLiteral("executable"), true);
        files.append(o);
    }
    return {{QStringLiteral("format"), QStringLiteral("cruciblecode-template")}, {QStringLiteral("version"), 1},
            {QStringLiteral("name"), t.name}, {QStringLiteral("description"), t.description}, {QStringLiteral("files"), files}};
}

static bool fromJson(const QJsonObject& o, ProjectTemplate* t, QString* error)
{
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("cruciblecode-template")) {
        if (error) *error = tr("This is not a CrucibleCode project template.");
        return false;
    }
    t->name = o.value(QStringLiteral("name")).toString();
    t->description = o.value(QStringLiteral("description")).toString();
    t->files.clear();
    for (const QJsonValue& v : o.value(QStringLiteral("files")).toArray()) {
        const QJsonObject fo = v.toObject();
        TemplateFile f;
        f.path = fo.value(QStringLiteral("path")).toString();
        if (f.path.isEmpty()) continue;
        if (fo.value(QStringLiteral("encoding")).toString() == QLatin1String("base64")) {
            f.text = false;
            f.data = QByteArray::fromBase64(fo.value(QStringLiteral("content")).toString().toLatin1());
        } else {
            f.data = fo.value(QStringLiteral("content")).toString().toUtf8();
        }
        f.executable = fo.value(QStringLiteral("executable")).toBool();
        t->files.append(f);
    }
    if (t->name.isEmpty()) { if (error) *error = tr("The template has no name."); return false; }
    return true;
}

static QString slug(const QString& name)
{
    QString s = name.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    s = s.mid(0, 60);
    while (s.startsWith(QLatin1Char('-'))) s.remove(0, 1);
    while (s.endsWith(QLatin1Char('-'))) s.chop(1);
    return s.isEmpty() ? QStringLiteral("template") : s;
}

QList<ProjectTemplate> all()
{
    QList<ProjectTemplate> l = builtins();
    const QFileInfoList files = QDir(userDirectory()).entryInfoList({QStringLiteral("*.cctemplate")}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        ProjectTemplate t;
        if (!fromJson(QJsonDocument::fromJson(f.readAll()).object(), &t, nullptr)) continue;
        t.id = fi.completeBaseName();
        l << t;
    }
    return l;
}

ProjectTemplate byId(const QString& id)
{
    for (const ProjectTemplate& t : all())
        if (t.id == id) return t;
    return {};
}

static bool writeJson(const ProjectTemplate& t, const QString& path, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    f.write(QJsonDocument(toJson(t)).toJson(QJsonDocument::Indented));
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

bool save(const ProjectTemplate& t, QString* error)
{
    ProjectTemplate copy = t;
    QString id = t.builtin || t.id.isEmpty() ? slug(t.name) : t.id;
    if (t.id.isEmpty() || t.builtin) {                       // new template: avoid overwriting another one
        const QString base = id;
        for (int n = 2; QFile::exists(userDirectory() + QLatin1Char('/') + id + QStringLiteral(".cctemplate")); ++n)
            id = base + QLatin1Char('-') + QString::number(n);
    }
    return writeJson(copy, userDirectory() + QLatin1Char('/') + id + QStringLiteral(".cctemplate"), error);
}

bool remove(const QString& id)
{
    if (id.startsWith(QLatin1String("builtin:"))) return false;
    if (defaultId() == id) setDefaultId(QStringLiteral("builtin:cpp-console"));
    return QFile::remove(userDirectory() + QLatin1Char('/') + id + QStringLiteral(".cctemplate"));
}

bool exportTo(const ProjectTemplate& t, const QString& path, QString* error) { return writeJson(t, path, error); }

bool importFrom(const QString& path, ProjectTemplate* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    ProjectTemplate t;
    if (!fromJson(QJsonDocument::fromJson(f.readAll()).object(), &t, error)) return false;
    if (!save(t, error)) return false;
    // find the id it was stored under
    for (const ProjectTemplate& u : all())
        if (!u.builtin && u.name == t.name) t.id = u.id;
    if (out) *out = t;
    return true;
}

// ---------------------------------------------------------------------------------------------
//  Folder -> template, template -> folder
// ---------------------------------------------------------------------------------------------

ProjectTemplate fromFolder(const QString& dir, const QString& name, const QString& description,
                           bool replaceFolderName, QString* error)
{
    ProjectTemplate t;
    const QDir root(dir);
    if (!root.exists()) { if (error) *error = tr("The folder does not exist."); return t; }
    const QString folderName = root.dirName();
    static const QStringList skipDirs = {QStringLiteral(".git"), QStringLiteral("node_modules"), QStringLiteral(".vs"),
        QStringLiteral("bin"), QStringLiteral("obj"), QStringLiteral("out"), QStringLiteral("__pycache__"), QStringLiteral(".cache")};

    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString rel = root.relativeFilePath(abs);
        const QStringList parts = rel.split(QLatin1Char('/'));
        bool skip = false;
        for (int i = 0; i + 1 < parts.size(); ++i)
            if (skipDirs.contains(parts.at(i)) || parts.at(i).startsWith(QLatin1String("build"))) skip = true;
        if (skip) continue;
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (f.size() > 2 * 1024 * 1024) continue;
        total += f.size();
        if (total > 20 * 1024 * 1024 || t.files.size() >= 2000) break;
        TemplateFile tfile;
        tfile.path = rel;
        tfile.data = f.readAll();
        tfile.text = !tfile.data.left(8192).contains('\0');
        tfile.executable = QFileInfo(abs).isExecutable() && tfile.text && (rel.endsWith(QLatin1String(".sh")));
        if (!tfile.text && tfile.data.size() > 512 * 1024) continue;
        if (replaceFolderName && !folderName.isEmpty()) {
            tfile.path.replace(folderName, QStringLiteral("{{name}}"));
            if (tfile.text) tfile.data = QString::fromUtf8(tfile.data).replace(folderName, QStringLiteral("{{name}}")).toUtf8();
        }
        t.files.append(tfile);
    }
    if (t.files.isEmpty()) { if (error) *error = tr("The folder contains no files that can be used."); return t; }
    t.id = slug(name);
    t.name = name;
    t.description = description;
    return t;
}

QString author()
{
    QString a = QSettings().value(QStringLiteral("templates/author")).toString();
    if (a.isEmpty()) a = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
    return a;
}
void setAuthor(const QString& a) { QSettings().setValue(QStringLiteral("templates/author"), a); }

static QString expand(QString s, const QString& name)
{
    QString ident = name;
    ident.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (ident.isEmpty() || ident.at(0).isDigit()) ident.prepend(QLatin1Char('_'));
    s.replace(QStringLiteral("{{name}}"), name);
    s.replace(QStringLiteral("{{NAME}}"), name.toUpper());
    s.replace(QStringLiteral("{{name_lower}}"), name.toLower());
    s.replace(QStringLiteral("{{identifier}}"), ident);
    s.replace(QStringLiteral("{{year}}"), QString::number(QDate::currentDate().year()));
    s.replace(QStringLiteral("{{date}}"), QDate::currentDate().toString(Qt::ISODate));
    s.replace(QStringLiteral("{{author}}"), author());
    return s;
}

QString create(const ProjectTemplate& t, const QString& parentDir, const QString& projectName, QString* error)
{
    const QString name = projectName.trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name == QLatin1String("..")) {
        if (error) *error = tr("Please enter a valid project name.");
        return {};
    }
    const QString target = QDir(parentDir).absoluteFilePath(name);
    QDir td(target);
    if (td.exists() && !td.isEmpty()) {
        if (error) *error = tr("The folder \"%1\" already exists and is not empty.").arg(QDir::toNativeSeparators(target));
        return {};
    }
    if (!QDir().mkpath(target)) {
        if (error) *error = tr("Could not create %1.").arg(QDir::toNativeSeparators(target));
        return {};
    }
    for (const TemplateFile& f : t.files) {
        const QString rel = QDir::cleanPath(expand(f.path, name));
        if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel.startsWith(QLatin1String("../")) || rel == QLatin1String("..")) continue;
        const QString dest = td.absoluteFilePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly)) {
            if (error) *error = tr("Could not write %1: %2").arg(rel, out.errorString());
            return {};
        }
        out.write(f.text ? expand(QString::fromUtf8(f.data), name).toUtf8() : f.data);
        out.close();
        if (f.executable) out.setPermissions(out.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    }
    return target;
}

// ---------------------------------------------------------------------------------------------

QString defaultId() { return QSettings().value(QStringLiteral("templates/default"), QStringLiteral("builtin:cpp-console")).toString(); }
void setDefaultId(const QString& id) { QSettings().setValue(QStringLiteral("templates/default"), id); }
bool alwaysUseDefault() { return QSettings().value(QStringLiteral("templates/alwaysDefault"), false).toBool(); }
void setAlwaysUseDefault(bool on) { QSettings().setValue(QStringLiteral("templates/alwaysDefault"), on); }

}
