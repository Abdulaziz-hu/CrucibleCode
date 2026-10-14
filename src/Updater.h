#pragma once
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QVersionNumber>

class QNetworkAccessManager;
class QNetworkReply;

struct ReleaseAsset {
    QString name;
    QString url;
    qint64 size = 0;
};

struct ReleaseInfo {
    QString tag;
    QVersionNumber version;
    QString title;
    QString notes;            // Markdown
    QString pageUrl;
    bool prerelease = false;
    QList<ReleaseAsset> assets;
    bool isValid() const { return !tag.isEmpty(); }
};

// Update system based on the GitHub Releases API: checks for a newer release, downloads the asset
// that matches how CrucibleCode was installed and applies it through a small helper script that
// waits for this process to exit, replaces the files and starts the new version.
class Updater : public QObject {
    Q_OBJECT
public:
    enum InstallType { Unsupported, AppImage, Deb, Rpm, WinPortable, WinInstaller, WinMsi };

    explicit Updater(QObject* parent = nullptr);

    static bool autoCheckEnabled();
    static void setAutoCheckEnabled(bool on);
    static bool includePrereleases();
    static void setIncludePrereleases(bool on);
    static QString skippedVersion();
    static void setSkippedVersion(const QString& v);

    static InstallType installType();
    static QString installTypeName(InstallType t);

    void check(bool manual);                      // result arrives through checkFinished()
    bool assetFor(const ReleaseInfo& r, ReleaseAsset* out) const;
    void download(const ReleaseAsset& asset, const ReleaseInfo& release);
    void cancelDownload();
    // Starts the helper that replaces the application. The caller must quit right afterwards.
    bool applyDownloaded(const QString& file, QString* error);

    static QString releasesApiUrl(bool latestOnly);

signals:
    // `release` is valid when a newer version exists. `error` is set when the check failed.
    void checkFinished(bool manual, bool updateAvailable, const ReleaseInfo& release, const QString& error);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString& file);
    void downloadFailed(const QString& error);
    void statusMessage(const QString& text);

private:
    void verifyAndFinish(const QString& file, const ReleaseAsset& asset, const ReleaseInfo& release);
    static ReleaseInfo parseRelease(const QByteArray& json, bool* ok);

    QNetworkAccessManager* m_net = nullptr;
    QPointer<QNetworkReply> m_download;
    QString m_downloadPath;
};
