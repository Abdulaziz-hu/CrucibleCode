#include "AiCompletion.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTcpServer>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <functional>

QList<AiCompletion::ModelPreset> AiCompletion::presets()
{
    return {
        {QStringLiteral("qwen-0.5b"),
         QStringLiteral("Qwen2.5-Coder 0.5B (default, ~530 MB, fastest)"),
         QStringLiteral("https://huggingface.co/ggml-org/Qwen2.5-Coder-0.5B-Q8_0-GGUF/resolve/main/qwen2.5-coder-0.5b-q8_0.gguf"),
         QStringLiteral("qwen2.5-coder-0.5b-q8_0.gguf")},
        {QStringLiteral("qwen-1.5b"),
         QStringLiteral("Qwen2.5-Coder 1.5B (~1.6 GB, better suggestions)"),
         QStringLiteral("https://huggingface.co/ggml-org/Qwen2.5-Coder-1.5B-Q8_0-GGUF/resolve/main/qwen2.5-coder-1.5b-q8_0.gguf"),
         QStringLiteral("qwen2.5-coder-1.5b-q8_0.gguf")},
    };
}

bool AiCompletion::enabledInSettings()
{
    return QSettings().value(QStringLiteral("ai/enabled"), false).toBool();
}

AiCompletion::AiCompletion(QObject* parent) : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
    m_server = new QProcess(this);
    m_health = new QTimer(this);
    m_health->setInterval(700);
    connect(m_health, &QTimer::timeout, this, &AiCompletion::pollHealth);
    connect(m_server, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        m_health->stop();
        if (m_state == Starting || m_state == Ready) setState(Failed, tr("the AI server stopped unexpectedly"));
    });
    connect(m_server, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) setState(Failed, tr("could not start llama-server"));
    });
}

AiCompletion::~AiCompletion()
{
    stop();
}

QString AiCompletion::dataDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir::cleanPath(base + QStringLiteral("/ai"));
}

QString AiCompletion::locateServer() const
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("llama-server.exe");
#else
    const QString name = QStringLiteral("llama-server");
#endif
    QDirIterator it(dataDir() + QStringLiteral("/runtime"), {name}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

QString AiCompletion::runtimePath() const
{
    QSettings s;
    const QString custom = s.value(QStringLiteral("ai/serverPath")).toString().trimmed();
    if (!custom.isEmpty() && QFileInfo::exists(custom)) return custom;
    const QString bundled = locateServer();
    if (!bundled.isEmpty()) return bundled;
    return QStandardPaths::findExecutable(QStringLiteral("llama-server"));
}

QString AiCompletion::modelPath() const
{
    QSettings s;
    const QString id = s.value(QStringLiteral("ai/model"), QStringLiteral("qwen-0.5b")).toString();
    if (id == QLatin1String("custom")) {
        const QString local = s.value(QStringLiteral("ai/customPath")).toString().trimmed();
        if (!local.isEmpty() && QFileInfo::exists(local)) return local;
        const QString url = s.value(QStringLiteral("ai/customUrl")).toString().trimmed();
        if (!url.isEmpty()) {
            const QString f = dataDir() + QStringLiteral("/models/") + QFileInfo(QUrl(url).path()).fileName();
            if (QFileInfo::exists(f)) return f;
        }
        return {};
    }
    for (const ModelPreset& p : presets()) {
        if (p.id == id) {
            const QString f = dataDir() + QStringLiteral("/models/") + p.fileName;
            return QFileInfo::exists(f) ? f : QString();
        }
    }
    return {};
}

QString AiCompletion::statusText() const
{
    switch (m_state) {
    case Disabled: return tr("AI completion is off");
    case NotReady:
        if (!runtimeAvailable()) return tr("AI: runtime not installed");
        if (!modelAvailable()) return tr("AI: model not downloaded");
        return tr("AI: stopped");
    case Starting: return tr("AI: starting...");
    case Ready: return tr("AI: ready");
    case Failed: return tr("AI: failed (%1)").arg(m_detail);
    }
    return {};
}

void AiCompletion::setState(State s, const QString& detail)
{
    m_state = s;
    m_detail = detail;
    emit stateChanged();
}

// ---- server ---------------------------------------------------------------------------------

void AiCompletion::applySettings()
{
    if (!enabledInSettings()) {
        stop();
        setState(Disabled);
        return;
    }
    if (m_state == Ready || m_state == Starting) return;
    if (!runtimeAvailable() || !modelAvailable()) {
        setState(NotReady);
        return;
    }
    start();
}

void AiCompletion::start()
{
    if (m_server->state() != QProcess::NotRunning) return;
    const QString exe = runtimePath();
    const QString model = modelPath();
    if (exe.isEmpty() || model.isEmpty()) { setState(NotReady); return; }

    QTcpServer probe;                          // find a free local port
    if (!probe.listen(QHostAddress::LocalHost, 0)) { setState(Failed, tr("no free port")); return; }
    m_port = probe.serverPort();
    probe.close();

    const int threads = qBound(2, QThread::idealThreadCount() / 2, 8);
    const QStringList args = {QStringLiteral("-m"), model, QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                              QStringLiteral("--port"), QString::number(m_port), QStringLiteral("-c"),
                              QStringLiteral("4096"), QStringLiteral("-t"), QString::number(threads)};

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString dir = QFileInfo(exe).absolutePath();
#if defined(Q_OS_WIN)
    env.insert(QStringLiteral("PATH"), dir + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
#elif defined(Q_OS_MACOS)
    env.insert(QStringLiteral("DYLD_LIBRARY_PATH"), dir + QLatin1Char(':') + env.value(QStringLiteral("DYLD_LIBRARY_PATH")));
#else
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), dir + QLatin1Char(':') + env.value(QStringLiteral("LD_LIBRARY_PATH")));
#endif
    m_server->setProcessEnvironment(env);
    m_server->setWorkingDirectory(dir);
    m_server->setProcessChannelMode(QProcess::MergedChannels);
    m_server->start(exe, args);
    m_healthTries = 0;
    setState(Starting);
    m_health->start();
}

void AiCompletion::stop()
{
    m_health->stop();
    if (m_request) { m_request->abort(); m_request = nullptr; }
    if (m_server->state() != QProcess::NotRunning) {
        m_server->terminate();
        if (!m_server->waitForFinished(1500)) m_server->kill();
        m_server->waitForFinished(1500);
    }
}

void AiCompletion::pollHealth()
{
    if (m_state != Starting) { m_health->stop(); return; }
    if (++m_healthTries > 130) {               // ~90 seconds
        m_health->stop();
        stop();
        setState(Failed, tr("timed out while loading the model"));
        return;
    }
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(m_port)));
    req.setTransferTimeout(1500);
    QNetworkReply* r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (m_state != Starting) return;
        if (r->error() == QNetworkReply::NoError
            && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            m_health->stop();
            setState(Ready);
        }
    });
}

quint64 AiCompletion::requestCompletion(const QString& prefix, const QString& suffix)
{
    if (m_state != Ready) return 0;
    if (m_request) { m_request->abort(); m_request = nullptr; }

    QJsonObject body;
    body.insert(QStringLiteral("input_prefix"), prefix);
    body.insert(QStringLiteral("input_suffix"), suffix);
    body.insert(QStringLiteral("n_predict"), 48);
    body.insert(QStringLiteral("temperature"), 0.1);
    body.insert(QStringLiteral("top_k"), 40);
    body.insert(QStringLiteral("top_p"), 0.9);
    body.insert(QStringLiteral("cache_prompt"), true);
    body.insert(QStringLiteral("stop"), QJsonArray{QStringLiteral("\n\n")});

    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/infill").arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(8000);
    const quint64 id = m_nextId++;
    m_activeId = id;
    QNetworkReply* r = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_request = r;
    connect(r, &QNetworkReply::finished, this, [this, r, id] {
        r->deleteLater();
        if (m_request == r) m_request = nullptr;
        if (r->error() != QNetworkReply::NoError || id != m_activeId) return;
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        emit completionReady(id, o.value(QStringLiteral("content")).toString());
    });
    return id;
}

// ---- downloads ----------------------------------------------------------------------------------

void AiCompletion::download(const QUrl& url, const QString& destFile, const QString& what,
                            std::function<void(bool, QString)> done)
{
    if (m_reply) { emit message(tr("A download is already running.")); return; }
    QDir().mkpath(QFileInfo(destFile).absolutePath());
    const QString part = destFile + QStringLiteral(".part");
    auto* file = new QFile(part, this);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        done(false, tr("Cannot write to %1").arg(part));
        return;
    }
    m_out = file;

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CrucibleCode"));
    m_reply = m_net->get(req);

    connect(m_reply, &QNetworkReply::readyRead, this, [this] { if (m_out && m_reply) m_out->write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, what](qint64 got, qint64 total) {
        emit progress(what, got, total);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, destFile, part, done] {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        if (m_out) { m_out->write(r->readAll()); m_out->close(); }
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = r->error() == QNetworkReply::NoError && (status == 0 || status == 200);
        const QString err = r->errorString();
        r->deleteLater();
        if (m_out) { m_out->deleteLater(); m_out = nullptr; }
        if (!ok) {
            QFile::remove(part);
            done(false, status == 404 ? tr("File not found on the server (HTTP 404). Check the model URL.") : err);
            return;
        }
        QFile::remove(destFile);
        if (!QFile::rename(part, destFile)) { done(false, tr("Could not save %1").arg(destFile)); return; }
        done(true, destFile);
    });
}

void AiCompletion::cancelDownload()
{
    if (m_reply) m_reply->abort();
}

void AiCompletion::downloadModel()
{
    QSettings s;
    const QString id = s.value(QStringLiteral("ai/model"), QStringLiteral("qwen-0.5b")).toString();
    QString url, fileName;
    if (id == QLatin1String("custom")) {
        url = s.value(QStringLiteral("ai/customUrl")).toString().trimmed();
        fileName = QFileInfo(QUrl(url).path()).fileName();
    } else {
        for (const ModelPreset& p : presets())
            if (p.id == id) { url = p.url; fileName = p.fileName; }
    }
    if (url.isEmpty() || fileName.isEmpty()) {
        emit downloadFinished(false, tr("No model URL set. Enter a download URL or choose a local .gguf file."));
        return;
    }
    download(QUrl(url), dataDir() + QStringLiteral("/models/") + fileName, tr("model"),
             [this](bool ok, const QString& text) {
                 emit downloadFinished(ok, ok ? tr("Model downloaded.") : text);
                 emit stateChanged();
             });
}

// Chooses the llama.cpp release asset for this OS / CPU from the GitHub release list.
static QString pickAsset(const QJsonArray& assets)
{
#if defined(Q_OS_WIN)
    const QString os = QStringLiteral("win");
#elif defined(Q_OS_MACOS)
    const QString os = QStringLiteral("macos");
#else
    const QString os = QStringLiteral("ubuntu");
#endif
    const bool arm = QSysInfo::currentCpuArchitecture().contains(QLatin1String("arm"));
    const QString arch = arm ? QStringLiteral("arm64") : QStringLiteral("x64");
    static const QStringList excluded = {"cuda", "cudart", "vulkan", "hip", "sycl", "opencl", "kompute", "openvino",
                                         "musa", "rocm", "xcframework", "kleidiai", "android", "s390x", "ppc64"};

    QString best;
    int bestScore = -1;
    for (const QJsonValue& v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(QStringLiteral("name")).toString().toLower();
        if (!name.contains(QLatin1String("bin")) || !name.contains(os) || !name.contains(arch)) continue;
        if (!(name.endsWith(QLatin1String(".zip")) || name.endsWith(QLatin1String(".tar.gz")) || name.endsWith(QLatin1String(".tgz"))))
            continue;
        bool bad = false;
        for (const QString& x : excluded) if (name.contains(x)) bad = true;
        if (bad) continue;
        int score = 10;
        if (name.contains(QLatin1String("avx2"))) score += 5;
        if (name.contains(QLatin1String("cpu"))) score += 3;
        if (name.contains(QLatin1String("noavx"))) score -= 5;
        score -= int(name.size() / 20);
        if (score > bestScore) { bestScore = score; best = a.value(QStringLiteral("browser_download_url")).toString(); }
    }
    return best;
}

void AiCompletion::downloadRuntime()
{
    if (m_reply) { emit message(tr("A download is already running.")); return; }
    emit message(tr("Looking up the latest llama.cpp release..."));

    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/ggml-org/llama.cpp/releases/latest")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CrucibleCode"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            emit downloadFinished(false, tr("Could not reach GitHub: %1").arg(r->errorString()));
            return;
        }
        const QJsonObject rel = QJsonDocument::fromJson(r->readAll()).object();
        const QString url = pickAsset(rel.value(QStringLiteral("assets")).toArray());
        if (url.isEmpty()) {
            emit downloadFinished(false, tr("No llama.cpp build for this platform was found. Install llama-server "
                                            "yourself (for example 'brew install llama.cpp') or choose its path."));
            return;
        }
        const QString fileName = QFileInfo(QUrl(url).path()).fileName();
        const QString dest = dataDir() + QStringLiteral("/runtime/") + fileName;
        emit message(tr("Downloading %1").arg(fileName));
        download(QUrl(url), dest, tr("runtime"), [this](bool ok, const QString& text) {
            if (!ok) { emit downloadFinished(false, text); return; }
            extractRuntime(text);
        });
    });
}

void AiCompletion::extractRuntime(const QString& archive)
{
    const QString dest = QFileInfo(archive).absolutePath();
    emit message(tr("Unpacking..."));

    QString program;
    QStringList args;
    if (archive.endsWith(QLatin1String(".zip"))) {
#ifdef Q_OS_LINUX
        if (!QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty()) {
            program = QStringLiteral("unzip");
            args = {QStringLiteral("-o"), QStringLiteral("-q"), archive, QStringLiteral("-d"), dest};
        } else if (!QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
            program = QStringLiteral("bsdtar");
            args = {QStringLiteral("-xf"), archive, QStringLiteral("-C"), dest};
        } else {
            program = QStringLiteral("python3");
            args = {QStringLiteral("-m"), QStringLiteral("zipfile"), QStringLiteral("-e"), archive, dest};
        }
#elif defined(Q_OS_MACOS)
        program = QStringLiteral("unzip");
        args = {QStringLiteral("-o"), QStringLiteral("-q"), archive, QStringLiteral("-d"), dest};
#else
        program = QStringLiteral("tar");    // bsdtar ships with Windows 10+
        args = {QStringLiteral("-xf"), archive, QStringLiteral("-C"), dest};
#endif
    } else {
        program = QStringLiteral("tar");
        args = {QStringLiteral("-xzf"), archive, QStringLiteral("-C"), dest};
    }

    auto* p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, archive](int code, QProcess::ExitStatus st) {
        p->deleteLater();
        const QString server = locateServer();
        if (code != 0 || st != QProcess::NormalExit || server.isEmpty()) {
            emit downloadFinished(false, tr("Could not unpack the runtime (llama-server not found in the archive)."));
            return;
        }
        QFile::setPermissions(server, QFile::permissions(server) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        QFile::remove(archive);
        emit downloadFinished(true, tr("Runtime installed."));
        emit stateChanged();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, program](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            emit downloadFinished(false, tr("Could not run '%1' to unpack the archive.").arg(program));
        }
    });
    p->start(program, args);
}

void AiCompletion::removeDownloads()
{
    stop();
    QDir(dataDir()).removeRecursively();
    emit message(tr("Downloaded AI files removed."));
    applySettings();
    emit stateChanged();
}
