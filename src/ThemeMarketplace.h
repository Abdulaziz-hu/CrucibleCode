#pragma once
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

// One entry of the marketplace index.json.
struct MarketTheme {
    QString id;                // "neon-synthwave" (also the file name: themes/<id>.css)
    QString name, author, version, description, type, credits;   // type: "dark" / "light"
    QStringList tags;
    QString file() const { return QStringLiteral("themes/%1.css").arg(id); }
};

// Downloads the theme list and individual theme files from the CrucibleCode-Themes repository.
//
//   <base>/index.json           the list of themes (see the repository README for the schema)
//   <base>/themes/<id>.css      one theme
//
// Everything that comes from the network is validated (ids, sizes, the CSS itself) before it is used,
// and the last good index / theme files are cached so the marketplace still opens offline.
// For development the base can be changed with the CRUCIBLE_THEMES_URL environment variable (or the
// "themes/marketplaceUrl" setting): an http(s) URL or a local folder, e.g. /home/me/CrucibleCode-Themes.
class ThemeMarketplace : public QObject {
    Q_OBJECT
public:
    explicit ThemeMarketplace(QObject* parent = nullptr);

    static QString baseUrl();       // no trailing slash
    static QString repoUrl();       // the GitHub page, where people open pull requests

    const QList<MarketTheme>& themes() const { return m_themes; }
    MarketTheme find(const QString& id) const;

    bool loadCachedIndex();         // true when a previously downloaded index was available
    void fetchIndex();
    void fetchTheme(const MarketTheme& theme);        // result: themeFetched() / themeFailed()
    QByteArray cachedCss(const MarketTheme& theme) const;
    bool isFetching(const QString& id) const { return m_inflight.contains(id); }

signals:
    void indexLoaded(int ignoredEntries);
    void indexFailed(const QString& error);
    void themeFetched(const QString& id, const QByteArray& css);
    void themeFailed(const QString& id, const QString& error);

private:
    QNetworkReply* get(const QUrl& url, qint64 maxBytes);
    static QString errorText(QNetworkReply* reply);
    bool parseIndex(const QByteArray& json, int* ignored, QString* error);
    QString cacheFile(const MarketTheme& t) const;

    QNetworkAccessManager* m_net = nullptr;
    QList<MarketTheme> m_themes;
    QSet<QString> m_inflight;
};
