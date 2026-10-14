#include "Updater.h"
#include "AppInfo.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>

Updater::Updater(QObject* parent) : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

// ---- settings --------------------------------------------------------------------------------

bool Updater::autoCheckEnabled() { return QSettings().value(QStringLiteral("update/autoCheck"), true).toBool(); }
void Updater::setAutoCheckEnabled(bool on) { QSettings().setValue(QStringLiteral("update/autoCheck"), on); }
bool Updater::includePrereleases() { return QSettings().value(QStringLiteral("update/prereleases"), false).toBool(); }
void Updater::setIncludePrereleases(bool on) { QSettings().setValue(QStringLiteral("update/prereleases"), on); }
QString Updater::skippedVersion() { return QSettings().value(QStringLiteral("update/skipped")).toString(); }
void Updater::setSkippedVersion(const QString& v) { QSettings().setValue(QStringLiteral("update/skipped"), v); }

// ---- how was this copy installed? --------------------------------------------------------------

static bool commandSucceeds(const QString& program, const QStringList& args)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(4000)) { p.kill(); return false; }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

Updater::InstallType Updater::installType()
{
#if defined(Q_OS_LINUX)
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo::exists(appImage)) return AppImage;
    const QString exe = QCoreApplication::applicationFilePath();
    if (exe.startsWith(QLatin1String("/usr/")) || exe.startsWith(QLatin1String("/opt/"))) {
        if (commandSucceeds(QStringLiteral("dpkg"), {QStringLiteral("-S"), exe})) return Deb;
        if (commandSucceeds(QStringLiteral("rpm"), {QStringLiteral("-qf"), exe})) return Rpm;
    }
    return Unsupported;
#elif defined(Q_OS_WIN)
    // The Windows packages ship an "install-type.txt" next to the exe (portable / installer / msi).
    QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/install-type.txt"));
    if (f.open(QIODevice::ReadOnly)) {
        const QString t = QString::fromUtf8(f.readAll()).trimmed().toLower();
        if (t == QLatin1String("portable")) return WinPortable;
        if (t == QLatin1String("installer")) return WinInstaller;
        if (t == QLatin1String("msi")) return WinMsi;
    }
    return Unsupported;
#else
    return Unsupported;
#endif
}

QString Updater::installTypeName(InstallType t)
{
    switch (t) {
    case AppImage: return QStringLiteral("AppImage");
    case Deb: return QStringLiteral(".deb package");
    case Rpm: return QStringLiteral(".rpm package");
    case WinPortable: return QStringLiteral("portable ZIP");
    case WinInstaller: return QStringLiteral("installer");
    case WinMsi: return QStringLiteral("MSI package");
    default: return QStringLiteral("unsupported (built from source)");
    }
}

// ---- checking -----------------------------------------------------------------------------------

QString Updater::releasesApiUrl(bool latestOnly)
{
    QString repo = QString::fromLatin1(kRepoUrl);                  // https://github.com/owner/name
    repo = repo.mid(repo.indexOf(QLatin1String("github.com/")) + 11);
    while (repo.endsWith(QLatin1Char('/'))) repo.chop(1);
    return QStringLiteral("https://api.github.com/repos/%1/releases").arg(repo) + (latestOnly ? QStringLiteral("/latest") : QStringLiteral("?per_page=10"));
}

ReleaseInfo Updater::parseRelease(const QByteArray& json, bool* ok)
{
    ReleaseInfo r;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    *ok = !o.isEmpty() && o.contains(QStringLiteral("tag_name"));
    if (!*ok) return r;
    r.tag = o.value(QStringLiteral("tag_name")).toString();
    QString v = r.tag;
    if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) v.remove(0, 1);
    r.version = QVersionNumber::fromString(v);
    r.title = o.value(QStringLiteral("name")).toString(r.tag);
    r.notes = o.value(QStringLiteral("body")).toString();
    r.pageUrl = o.value(QStringLiteral("html_url")).toString();
    r.prerelease = o.value(QStringLiteral("prerelease")).toBool();
    for (const QJsonValue& av : o.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject a = av.toObject();
        ReleaseAsset as;
        as.name = a.value(QStringLiteral("name")).toString();
        as.url = a.value(QStringLiteral("browser_download_url")).toString();
        as.size = qint64(a.value(QStringLiteral("size")).toDouble());
        if (!as.name.isEmpty() && !as.url.isEmpty()) r.assets.append(as);
    }
    return r;
}

void Updater::check(bool manual)
{
    const bool pre = includePrereleases();
    QNetworkRequest req{QUrl(releasesApiUrl(!pre))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setRawHeader("User-Agent", QByteArray("CrucibleCode/") + QCoreApplication::applicationVersion().toUtf8());
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual, pre] {
        reply->deleteLater();
        QSettings().setValue(QStringLiteral("update/lastCheck"), QDateTime::currentDateTimeUtc());
        if (reply->error() != QNetworkReply::NoError) {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString err = code == 404 ? tr("No release has been published yet.") : reply->errorString();
            if (code == 403 || code == 429) err = tr("GitHub refused the request (rate limit). Try again later.");
            emit checkFinished(manual, false, {}, err);
            return;
        }
        const QByteArray data = reply->readAll();
        ReleaseInfo rel;
        bool ok = false;
        if (pre) {                                                // /releases returns an array, newest first
            for (const QJsonValue& v : QJsonDocument::fromJson(data).array()) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("draft")).toBool()) continue;
                rel = parseRelease(QJsonDocument(o).toJson(), &ok);
                if (ok) break;
            }
        } else {
            rel = parseRelease(data, &ok);
        }
        if (!ok) { emit checkFinished(manual, false, {}, tr("Unexpected answer from GitHub.")); return; }
        const QVersionNumber current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        emit checkFinished(manual, rel.version > current, rel, QString());
    });
}

bool Updater::assetFor(const ReleaseInfo& r, ReleaseAsset* out) const
{
    const InstallType t = installType();
    const QString arch = QSysInfo::currentCpuArchitecture();
    const bool arm = arch.contains(QLatin1String("arm")) || arch.contains(QLatin1String("aarch64"));
    ReleaseAsset best;
    int bestScore = -1;
    for (const ReleaseAsset& a : r.assets) {
        const QString n = a.name.toLower();
        bool match = false;
        switch (t) {
        case AppImage: match = n.endsWith(QLatin1String(".appimage")); break;
        case Deb: match = n.endsWith(QLatin1String(".deb")); break;
        case Rpm: match = n.endsWith(QLatin1String(".rpm")) && !n.endsWith(QLatin1String(".src.rpm")); break;
        case WinPortable: match = n.endsWith(QLatin1String(".zip")) && (n.contains(QLatin1String("win")) || n.contains(QLatin1String("portable"))); break;
        case WinInstaller: match = n.endsWith(QLatin1String(".exe")) && (n.contains(QLatin1String("setup")) || n.contains(QLatin1String("install"))); break;
        case WinMsi: match = n.endsWith(QLatin1String(".msi")); break;
        case Unsupported: break;
        }
        if (!match) continue;
        int score = 1;
        const bool isArm = n.contains(QLatin1String("aarch64")) || n.contains(QLatin1String("arm64"));
        if (isArm == arm) score += 2;
        if (!arm && (n.contains(QLatin1String("x86_64")) || n.contains(QLatin1String("amd64")) || n.contains(QLatin1String("x64")))) score += 1;
        if (t == WinPortable && n.contains(QLatin1String("portable"))) score += 1;
        if (score > bestScore) { bestScore = score; best = a; }
    }
    if (bestScore < 0) return false;
    if (out) *out = best;
    return true;
}

// ---- downloading ----------------------------------------------------------------------------------

void Updater::download(const ReleaseAsset& asset, const ReleaseInfo& release)
{
    cancelDownload();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/updates");
    QDir().mkpath(dir);
    m_downloadPath = dir + QLatin1Char('/') + asset.name;
    auto* file = new QFile(m_downloadPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        emit downloadFailed(tr("Cannot write %1: %2").arg(m_downloadPath, file->errorString()));
        file->deleteLater();
        return;
    }
    QNetworkRequest req{QUrl(asset.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", QByteArray("CrucibleCode/") + QCoreApplication::applicationVersion().toUtf8());
    req.setRawHeader("Accept", "application/octet-stream");
    QNetworkReply* reply = m_net->get(req);
    m_download = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, asset](qint64 got, qint64 total) {
        emit downloadProgress(got, total > 0 ? total : asset.size);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, asset, release] {
        reply->deleteLater();
        file->write(reply->readAll());
        file->close();
        file->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError) { QFile::remove(m_downloadPath); return; }
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(m_downloadPath);
            emit downloadFailed(reply->errorString());
            return;
        }
        verifyAndFinish(m_downloadPath, asset, release);
    });
}

void Updater::cancelDownload()
{
    if (m_download) m_download->abort();
}

// Size check, plus a SHA-256 check when the release carries "<asset>.sha256".
void Updater::verifyAndFinish(const QString& path, const ReleaseAsset& asset, const ReleaseInfo& release)
{
    if (asset.size > 0 && QFileInfo(path).size() != asset.size) {
        QFile::remove(path);
        emit downloadFailed(tr("The downloaded file has the wrong size (%1 instead of %2 bytes).").arg(QFileInfo(path).size()).arg(asset.size));
        return;
    }
    const ReleaseAsset* sumAsset = nullptr;
    for (const ReleaseAsset& a : release.assets)
        if (a.name.compare(asset.name + QStringLiteral(".sha256"), Qt::CaseInsensitive) == 0) sumAsset = &a;
    if (!sumAsset) { emit downloadFinished(path); return; }

    emit statusMessage(tr("Verifying checksum..."));
    QNetworkRequest req{QUrl(sumAsset->url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", QByteArray("CrucibleCode/") + QCoreApplication::applicationVersion().toUtf8());
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit downloadFinished(path); return; }   // checksum file unavailable: size check only
        const QString expected = QString::fromUtf8(reply->readAll()).trimmed().split(QRegularExpression(QStringLiteral("\\s+"))).value(0).toLower();
        QFile f(path);
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (f.open(QIODevice::ReadOnly)) h.addData(&f);
        if (!expected.isEmpty() && QString::fromLatin1(h.result().toHex()) != expected) {
            QFile::remove(path);
            emit downloadFailed(tr("The checksum of the downloaded file does not match. The file was removed."));
            return;
        }
        emit downloadFinished(path);
    });
}

// ---- applying ---------------------------------------------------------------------------------------

static bool writeScript(const QString& path, const QString& text, bool executable)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(text.toUtf8());
    f.close();
    if (executable) f.setPermissions(f.permissions() | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
    return true;
}

bool Updater::applyDownloaded(const QString& file, QString* error)
{
    const InstallType t = installType();
    const qint64 pid = QCoreApplication::applicationPid();
    const QString exe = QCoreApplication::applicationFilePath();
    const QString work = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/cruciblecode-update-%1").arg(pid);
    QDir().mkpath(work);

#if defined(Q_OS_WIN)
    const QString ps1 = work + QStringLiteral("/update.ps1");
    const QString script = QStringLiteral(
        "param([int]$ProcId, [string]$Package, [string]$AppDir, [string]$Exe, [string]$Type)\n"
        "$ErrorActionPreference = 'Continue'\n"
        "$log = Join-Path $env:TEMP 'cruciblecode-update.log'\n"
        "function Log($m) { Add-Content -Path $log -Value (\"{0} {1}\" -f (Get-Date -Format s), $m) }\n"
        "try { Wait-Process -Id $ProcId -Timeout 120 -ErrorAction SilentlyContinue } catch {}\n"
        "Start-Sleep -Milliseconds 700\n"
        "try {\n"
        "  switch ($Type) {\n"
        "    'portable' {\n"
        "      $tmp = Join-Path $env:TEMP ('cruciblecode-update-' + [guid]::NewGuid().ToString('N'))\n"
        "      Expand-Archive -LiteralPath $Package -DestinationPath $tmp -Force\n"
        "      $src = $tmp\n"
        "      $items = @(Get-ChildItem -LiteralPath $tmp)\n"
        "      if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $src = $items[0].FullName }\n"
        "      Copy-Item -Path (Join-Path $src '*') -Destination $AppDir -Recurse -Force\n"
        "      Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue\n"
        "    }\n"
        "    'installer' { Start-Process -FilePath $Package -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS' -Wait }\n"
        "    'msi' { Start-Process -FilePath 'msiexec.exe' -ArgumentList '/i', ('\"' + $Package + '\"'), '/passive', '/norestart' -Wait }\n"
        "  }\n"
        "  Log \"update applied ($Type)\"\n"
        "} catch { Log \"update failed: $_\" }\n"
        "Start-Process -FilePath $Exe\n");
    if (!writeScript(ps1, script, false)) { if (error) *error = tr("Could not write the update helper."); return false; }
    QString type = t == WinPortable ? QStringLiteral("portable") : t == WinInstaller ? QStringLiteral("installer") : QStringLiteral("msi");
    const bool ok = QProcess::startDetached(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
         QStringLiteral("-File"), QDir::toNativeSeparators(ps1), QStringLiteral("-ProcId"), QString::number(pid),
         QStringLiteral("-Package"), QDir::toNativeSeparators(file), QStringLiteral("-AppDir"), QDir::toNativeSeparators(QCoreApplication::applicationDirPath()),
         QStringLiteral("-Exe"), QDir::toNativeSeparators(exe), QStringLiteral("-Type"), type});
    if (!ok && error) *error = tr("Could not start the update helper (powershell.exe).");
    return ok;
#else
    const QString sh = work + QStringLiteral("/update.sh");
    QString script;
    if (t == AppImage) {
        script = QStringLiteral(
            "#!/bin/sh\n"
            "PID=\"$1\"; NEW=\"$2\"; TARGET=\"$3\"\n"
            "while kill -0 \"$PID\" 2>/dev/null; do sleep 0.3; done\n"
            "sleep 0.5\n"
            "if cp -f \"$NEW\" \"$TARGET.new\" && chmod +x \"$TARGET.new\" && mv -f \"$TARGET.new\" \"$TARGET\"; then rm -f \"$NEW\"; fi\n"
            "nohup \"$TARGET\" >/dev/null 2>&1 &\n");
    } else if (t == Deb || t == Rpm) {
        script = QStringLiteral(
            "#!/bin/sh\n"
            "PID=\"$1\"; NEW=\"$2\"; EXE=\"$3\"; KIND=\"$4\"\n"
            "while kill -0 \"$PID\" 2>/dev/null; do sleep 0.3; done\n"
            "sleep 0.5\n"
            "if [ \"$(id -u)\" = 0 ]; then SUDO=\"\"; elif command -v pkexec >/dev/null 2>&1; then SUDO=\"pkexec\"; else SUDO=\"sudo\"; fi\n"
            "if [ \"$KIND\" = deb ]; then\n"
            "  $SUDO apt-get install -y --allow-downgrades \"$NEW\" || $SUDO dpkg -i \"$NEW\"\n"
            "else\n"
            "  if command -v dnf >/dev/null 2>&1; then $SUDO dnf install -y \"$NEW\"; elif command -v zypper >/dev/null 2>&1; then $SUDO zypper --non-interactive install --allow-unsigned-rpm \"$NEW\"; else $SUDO rpm -U --replacepkgs \"$NEW\"; fi\n"
            "fi\n"
            "nohup \"$EXE\" >/dev/null 2>&1 &\n");
    } else {
        if (error) *error = tr("This installation type cannot be updated automatically.");
        return false;
    }
    if (!writeScript(sh, script, true)) { if (error) *error = tr("Could not write the update helper."); return false; }
    QStringList args{QString::number(pid), file};
    if (t == AppImage) args << qEnvironmentVariable("APPIMAGE");
    else args << exe << (t == Deb ? QStringLiteral("deb") : QStringLiteral("rpm"));
    const bool ok = QProcess::startDetached(QStringLiteral("/bin/sh"), QStringList{sh} + args);
    if (!ok && error) *error = tr("Could not start the update helper.");
    return ok;
#endif
}
