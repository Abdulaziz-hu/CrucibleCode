#include "ThemeLibrary.h"
#include "ThemeCss.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVersionNumber>
#include <algorithm>

static QString T(const char* s) { return QCoreApplication::translate("ThemeLibrary", s); }

// ---- locations -------------------------------------------------------------------------------

QString ThemeLibrary::themesDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/themes");
    QDir().mkpath(dir);
    return dir;
}

QString ThemeLibrary::cacheDir()
{
    const QString dir = themesDir() + QStringLiteral("/cache");
    QDir().mkpath(dir);
    return dir;
}

// ---- ids -------------------------------------------------------------------------------------

bool ThemeLibrary::validMarketplaceId(const QString& id)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9-]{0,47}$"));
    return re.match(id).hasMatch();
}

QString ThemeLibrary::sanitizeId(const QString& text)
{
    QString out;
    for (const QChar ch : text.toLower()) {
        const bool alnum = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'));
        if (alnum) out += ch;
        else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-'))) out += QLatin1Char('-');
    }
    while (out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out.left(48);
}

// Any file name that cannot escape the themes folder (files dropped in by hand may use spaces, capitals, ...).
bool ThemeLibrary::safeFileId(const QString& id)
{
    return !id.isEmpty() && id.size() <= 100 && !id.startsWith(QLatin1Char('.')) && !id.contains(QLatin1String(".."))
           && !id.contains(QLatin1Char('/')) && !id.contains(QLatin1Char('\\')) && !id.contains(QLatin1Char(':'));
}

int ThemeLibrary::compareVersions(const QString& a, const QString& b)
{
    const auto norm = [](QString v) {
        v = v.trimmed();
        if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) v.remove(0, 1);
        return QVersionNumber::fromString(v);
    };
    return QVersionNumber::compare(norm(a), norm(b));
}

// ---- files -----------------------------------------------------------------------------------

static QByteArray readFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return {};
    }
    if (f.size() > ThemeCss::kMaxFileBytes) {
        if (error) *error = T("The theme file is too large.");
        return {};
    }
    return f.readAll();
}

static bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

// installed.json remembers where an installed theme came from and which marketplace version it is
static QString manifestPath() { return ThemeLibrary::themesDir() + QStringLiteral("/installed.json"); }

static QJsonObject readManifest()
{
    QFile f(manifestPath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("themes")).toObject();
}

static bool writeManifest(const QJsonObject& themes, QString* error)
{
    const QJsonObject root{{QStringLiteral("themes"), themes}};
    return writeFile(manifestPath(), QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

static ThemeInfo infoFromFile(const QString& path, const QString& id, const QString& key, const QJsonObject& m)
{
    QString err;
    const QByteArray data = readFile(path, &err);
    if (data.isEmpty()) return {};
    const ThemeCss::Result r = ThemeCss::parse(QString::fromUtf8(data));
    if (!r.ok) return {};
    ThemeInfo t;
    t.key = key;
    t.id = id;
    t.path = path;
    const auto pick = [&m](const char* k, const QString& fallback) {
        const QString v = m.value(QLatin1String(k)).toString();
        return v.isEmpty() ? fallback : v;
    };
    t.name = pick("name", r.meta.name.isEmpty() ? id : r.meta.name);
    t.author = pick("author", r.meta.author);
    t.version = pick("version", r.meta.version);
    t.description = pick("description", r.meta.description);
    t.credits = pick("credits", r.meta.credits);
    t.type = pick("type", r.meta.type);
    t.local = m.value(QStringLiteral("source")).toString() == QLatin1String("local");
    return t;
}

// ---- built-in themes -------------------------------------------------------------------------

QList<ThemeInfo> ThemeLibrary::builtinThemes()
{
    static QList<ThemeInfo> cache;
    if (!cache.isEmpty()) return cache;

    ThemeInfo dark;
    dark.key = QStringLiteral("dark");
    dark.id = QStringLiteral("dark");
    dark.name = T("Dark Grey");
    dark.author = QStringLiteral("CrucibleCode");
    dark.type = QStringLiteral("dark");
    dark.description = T("The default dark theme.");
    dark.builtin = true;
    ThemeInfo light = dark;
    light.key = light.id = QStringLiteral("light");
    light.name = T("Light");
    light.type = QStringLiteral("light");
    light.description = T("The default light theme.");

    QList<ThemeInfo> css;
    const QDir dir(QStringLiteral(":/themes"));
    for (const QString& file : dir.entryList({QStringLiteral("*.css")}, QDir::Files)) {
        const QString id = QFileInfo(file).completeBaseName();
        const ThemeInfo t = infoFromFile(QStringLiteral(":/themes/") + file, id, builtinKey(id), {});
        if (!t.isValid()) continue;
        ThemeInfo b = t;
        b.builtin = true;
        if (b.author.isEmpty()) b.author = QStringLiteral("CrucibleCode");
        css << b;
    }
    std::sort(css.begin(), css.end(), [](const ThemeInfo& a, const ThemeInfo& b) {
        if (a.type != b.type) return a.type == QLatin1String("dark");        // dark themes first
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    cache << dark << light << css;
    return cache;
}

// ---- installed themes ------------------------------------------------------------------------

QList<ThemeInfo> ThemeLibrary::installedThemes()
{
    QList<ThemeInfo> out;
    const QJsonObject manifest = readManifest();
    const QDir dir(themesDir());
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.css")}, QDir::Files)) {
        const QString id = fi.completeBaseName();
        if (!safeFileId(id)) continue;
        const ThemeInfo t = infoFromFile(fi.absoluteFilePath(), id, installedKey(id), manifest.value(id).toObject());
        if (t.isValid()) out << t;     // unreadable / broken files are simply not listed
    }
    std::sort(out.begin(), out.end(), [](const ThemeInfo& a, const ThemeInfo& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

ThemeInfo ThemeLibrary::installedInfo(const QString& id)
{
    if (!safeFileId(id)) return {};
    const QString path = themesDir() + QLatin1Char('/') + id + QStringLiteral(".css");
    if (!QFileInfo::exists(path)) return {};
    return infoFromFile(path, id, installedKey(id), readManifest().value(id).toObject());
}

ThemeInfo ThemeLibrary::info(const QString& key)
{
    if (key == QLatin1String("custom")) {
        ThemeInfo t;
        t.key = t.id = key;
        t.name = ThemeManager::instance().customName();
        t.builtin = true;
        return t;
    }
    if (key.startsWith(QLatin1String("installed:"))) return installedInfo(key.mid(10));
    for (const ThemeInfo& t : builtinThemes())
        if (t.key == key) return t;
    return {};
}

bool ThemeLibrary::exists(const QString& key)
{
    return info(key).isValid();
}

bool ThemeLibrary::install(const ThemeInfo& meta, const QByteArray& css, QString* error)
{
    if (!validMarketplaceId(meta.id)) {
        if (error) *error = T("The theme has an invalid id.");
        return false;
    }
    if (css.size() > ThemeCss::kMaxFileBytes) {
        if (error) *error = T("The theme file is too large.");
        return false;
    }
    const ThemeCss::Result r = ThemeCss::parse(QString::fromUtf8(css));
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    if (!writeFile(themesDir() + QLatin1Char('/') + meta.id + QStringLiteral(".css"), css, error)) return false;

    QJsonObject manifest = readManifest();
    manifest.insert(meta.id, QJsonObject{{QStringLiteral("name"), meta.name},
                                         {QStringLiteral("author"), meta.author},
                                         {QStringLiteral("version"), meta.version},
                                         {QStringLiteral("description"), meta.description},
                                         {QStringLiteral("credits"), meta.credits},
                                         {QStringLiteral("type"), meta.type.isEmpty() ? r.meta.type : meta.type},
                                         {QStringLiteral("source"), QStringLiteral("marketplace")}});
    return writeManifest(manifest, error);
}

bool ThemeLibrary::installFromFile(const QString& path, ThemeInfo* out, QString* error)
{
    const QByteArray data = readFile(path, error);
    if (data.isEmpty()) {
        if (error && error->isEmpty()) *error = T("The file is empty.");
        return false;
    }
    const ThemeCss::Result r = ThemeCss::parse(QString::fromUtf8(data));
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    QString id = sanitizeId(QFileInfo(path).completeBaseName());
    if (id.isEmpty()) id = sanitizeId(r.meta.name);
    if (id.isEmpty()) id = QStringLiteral("imported-theme");
    if (!writeFile(themesDir() + QLatin1Char('/') + id + QStringLiteral(".css"), data, error)) return false;

    QJsonObject manifest = readManifest();
    manifest.insert(id, QJsonObject{{QStringLiteral("name"), r.meta.name.isEmpty() ? id : r.meta.name},
                                    {QStringLiteral("author"), r.meta.author},
                                    {QStringLiteral("version"), r.meta.version},
                                    {QStringLiteral("description"), r.meta.description},
                                    {QStringLiteral("credits"), r.meta.credits},
                                    {QStringLiteral("type"), r.meta.type},
                                    {QStringLiteral("source"), QStringLiteral("local")}});
    if (!writeManifest(manifest, error)) return false;
    if (out) *out = installedInfo(id);
    return true;
}

bool ThemeLibrary::uninstall(const QString& id, QString* error)
{
    if (!safeFileId(id)) {
        if (error) *error = T("Invalid theme id.");
        return false;
    }
    const QString path = themesDir() + QLatin1Char('/') + id + QStringLiteral(".css");
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        if (error) *error = T("The theme file could not be deleted: %1").arg(path);
        return false;
    }
    QJsonObject manifest = readManifest();
    if (manifest.contains(id)) {
        manifest.remove(id);
        writeManifest(manifest, nullptr);
    }
    return true;
}

// ---- loading ---------------------------------------------------------------------------------

bool ThemeLibrary::load(const QString& key, ThemeColors* colors, QString* error)
{
    if (key == QLatin1String("dark")) { if (colors) *colors = ThemeManager::baseColors(ThemeManager::DarkGrey); return true; }
    if (key == QLatin1String("light")) { if (colors) *colors = ThemeManager::baseColors(ThemeManager::Light); return true; }
    if (key == QLatin1String("custom")) { if (colors) *colors = ThemeManager::instance().customColors(); return true; }

    QString path;
    if (key.startsWith(QLatin1String("builtin:"))) {
        const QString id = key.mid(8);
        if (!safeFileId(id)) return false;
        path = QStringLiteral(":/themes/") + id + QStringLiteral(".css");
    } else if (key.startsWith(QLatin1String("installed:"))) {
        const QString id = key.mid(10);
        if (!safeFileId(id)) return false;
        path = themesDir() + QLatin1Char('/') + id + QStringLiteral(".css");
    } else {
        if (error) *error = T("Unknown theme.");
        return false;
    }
    const QByteArray data = readFile(path, error);
    if (data.isEmpty()) return false;
    const ThemeCss::Result r = ThemeCss::parse(QString::fromUtf8(data));
    if (!r.ok) {
        if (error) *error = r.error;
        return false;
    }
    if (colors) *colors = r.colors;
    return true;
}
