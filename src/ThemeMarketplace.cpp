#include "ThemeMarketplace.h"
#include "AppInfo.h"
#include "ThemeCss.h"
#include "ThemeLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>

namespace {
constexpr int kSchema = 1;                  // index.json "schema" this build understands
constexpr qint64 kMaxIndexBytes = 2 * 1024 * 1024;
constexpr int kMaxThemes = 2000;
}

ThemeMarketplace::ThemeMarketplace(QObject* parent) : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

QString ThemeMarketplace::repoUrl() { return QString::fromLatin1(kThemesRepoUrl); }

QString ThemeMarketplace::baseUrl()
{
    // CRUCIBLE_THEMES_URL (environment) wins over the "themes/marketplaceUrl" setting; both are for development
    QString b = qEnvironmentVariable("CRUCIBLE_THEMES_URL").trimmed();
    if (b.isEmpty()) b = QSettings().value(QStringLiteral("themes/marketplaceUrl")).toString().trimmed();
    if (!b.isEmpty()) {
        if (QDir::isAbsolutePath(b)) b = QUrl::fromLocalFile(b).toString();           // plain folder for local testing
        const QString scheme = QUrl(b).scheme().toLower();
        if (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("file")) b.clear();
    }
    if (b.isEmpty()) b = QString::fromLatin1(kThemesRawUrl);
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    return b;
}

MarketTheme ThemeMarketplace::find(const QString& id) const
{
    for (const MarketTheme& t : m_themes)
        if (t.id == id) return t;
    return {};
}

// ---- cache -------------------------------------------------------------------------------------

QString ThemeMarketplace::cacheFile(const MarketTheme& t) const
{
    static const QRegularExpression bad(QStringLiteral("[^0-9A-Za-z._-]"));
    QString v = t.version;
    v.replace(bad, QStringLiteral("_"));
    return ThemeLibrary::cacheDir() + QLatin1Char('/') + t.id + QLatin1Char('@') + v + QStringLiteral(".css");
}

QByteArray ThemeMarketplace::cachedCss(const MarketTheme& t) const
{
    QFile f(cacheFile(t));
    if (!f.open(QIODevice::ReadOnly) || f.size() > ThemeCss::kMaxFileBytes) return {};
    return f.readAll();
}

static void writeCache(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.commit();
    }
}

bool ThemeMarketplace::loadCachedIndex()
{
    QFile f(ThemeLibrary::cacheDir() + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxIndexBytes) return false;
    int ignored = 0;
    QString err;
    return parseIndex(f.readAll(), &ignored, &err);
}

// ---- index -------------------------------------------------------------------------------------

static QString clipped(const QJsonValue& v, int max)
{
    return v.toString().trimmed().left(max);
}

bool ThemeMarketplace::parseIndex(const QByteArray& json, int* ignored, QString* error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        *error = tr("The theme index is not valid JSON.");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema")).toInt(kSchema) > kSchema) {
        *error = tr("This theme repository needs a newer version of CrucibleCode.");
        return false;
    }
    QList<MarketTheme> list;
    QSet<QString> seen;
    int skipped = 0;
    const QJsonArray arr = root.value(QStringLiteral("themes")).toArray();
    for (const QJsonValue& v : arr) {
        if (list.size() >= kMaxThemes) { ++skipped; continue; }
        const QJsonObject o = v.toObject();
        MarketTheme t;
        t.id = o.value(QStringLiteral("id")).toString().trimmed();
        t.name = clipped(o.value(QStringLiteral("name")), 60);
        t.version = clipped(o.value(QStringLiteral("version")), 24);
        // the file always lives at themes/<id>.css; an entry that points elsewhere is rejected
        const QString file = o.value(QStringLiteral("file")).toString(t.file());
        if (!ThemeLibrary::validMarketplaceId(t.id) || seen.contains(t.id) || t.name.isEmpty() || t.version.isEmpty()
            || file != t.file()) {
            ++skipped;
            continue;
        }
        seen.insert(t.id);
        t.author = clipped(o.value(QStringLiteral("author")), 60);
        t.description = clipped(o.value(QStringLiteral("description")), 400);
        t.credits = clipped(o.value(QStringLiteral("credits")), 200);
        t.type = o.value(QStringLiteral("type")).toString().toLower() == QLatin1String("light") ? QStringLiteral("light") : QStringLiteral("dark");
        for (const QJsonValue& tag : o.value(QStringLiteral("tags")).toArray())
            if (t.tags.size() < 8 && !tag.toString().trimmed().isEmpty()) t.tags << tag.toString().trimmed().left(24);
        list << t;
    }
    m_themes = list;
    *ignored = skipped;
    return true;
}

QString ThemeMarketplace::errorText(QNetworkReply* r)
{
    if (r->property("tooLarge").toBool()) return tr("The download is larger than expected.");
    const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 404) return tr("Not found in the theme repository (404).");
    if (code == 403 || code == 429) return tr("GitHub refused the request (rate limit). Try again later.");
    return r->errorString();
}

QNetworkReply* ThemeMarketplace::get(const QUrl& url, qint64 maxBytes)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", QByteArray("CrucibleCode/") + QCoreApplication::applicationVersion().toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
        if (received > maxBytes) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        }
    });
    return reply;
}

void ThemeMarketplace::fetchIndex()
{
    QNetworkReply* reply = get(QUrl(baseUrl() + QStringLiteral("/index.json")), kMaxIndexBytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit indexFailed(errorText(reply));
            return;
        }
        const QByteArray data = reply->readAll();
        int ignored = 0;
        QString err;
        if (!parseIndex(data, &ignored, &err)) {
            emit indexFailed(err);
            return;
        }
        writeCache(ThemeLibrary::cacheDir() + QStringLiteral("/index.json"), data);
        emit indexLoaded(ignored);
    });
}

// ---- theme files -------------------------------------------------------------------------------

void ThemeMarketplace::fetchTheme(const MarketTheme& theme)
{
    if (m_inflight.contains(theme.id)) return;
    m_inflight.insert(theme.id);
    QNetworkReply* reply = get(QUrl(baseUrl() + QLatin1Char('/') + theme.file()), ThemeCss::kMaxFileBytes);
    const MarketTheme t = theme;
    connect(reply, &QNetworkReply::finished, this, [this, reply, t] {
        reply->deleteLater();
        m_inflight.remove(t.id);
        if (reply->error() != QNetworkReply::NoError) {
            emit themeFailed(t.id, errorText(reply));
            return;
        }
        const QByteArray data = reply->readAll();
        const ThemeCss::Result r = ThemeCss::parse(QString::fromUtf8(data));
        if (!r.ok) {
            emit themeFailed(t.id, r.error);
            return;
        }
        writeCache(cacheFile(t), data);
        emit themeFetched(t.id, data);
    });
}
