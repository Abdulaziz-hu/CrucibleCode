#include "LiveServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTimer>

LiveServer::LiveServer(QObject* parent) : QObject(parent)
{
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    m_startTimeout = new QTimer(this);
    m_startTimeout->setSingleShot(true);
    m_startTimeout->setInterval(8000);

    connect(m_proc, &QProcess::readyRead, this, [this] {
        m_buffer += QString::fromLocal8Bit(m_proc->readAll());
        int nl;
        while ((nl = int(m_buffer.indexOf(QLatin1Char('\n')))) >= 0) {
            const QString line = m_buffer.left(nl).trimmed();
            m_buffer.remove(0, nl + 1);
            if (!line.isEmpty()) handleLine(line);
        }
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        m_startTimeout->stop();
        const bool wasActive = m_running || m_starting;
        m_running = false;
        m_starting = false;
        if (wasActive && code != 0) emit message(tr("Live server stopped (exit code %1).").arg(code));
        else if (wasActive) emit message(tr("Live server stopped."));
        emit stateChanged();
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_startTimeout->stop();
        m_running = m_starting = false;
        emit message(tr("Could not start Python for the live server."));
        emit stateChanged();
    });
    connect(m_startTimeout, &QTimer::timeout, this, [this] {
        if (m_running) return;
        emit message(tr("The live server did not start in time."));
        stop();
    });
}

LiveServer::~LiveServer()
{
    stop();
}

QUrl LiveServer::rootUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_port));
}

QUrl LiveServer::urlFor(const QString& filePath) const
{
    QUrl url = rootUrl();
    if (filePath.isEmpty() || m_root.isEmpty()) return url;
    const QFileInfo fi(filePath);
    const QString suffix = fi.suffix().toLower();
    if (suffix != QLatin1String("html") && suffix != QLatin1String("htm") && suffix != QLatin1String("xhtml")) return url;
    const QString rel = QDir(m_root).relativeFilePath(fi.absoluteFilePath());
    if (rel.startsWith(QLatin1String(".."))) return url;
    QString encoded;
    for (const QString& part : rel.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        encoded += QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(part));
    url.setPath(encoded);
    return url;
}

QString LiveServer::findPython(QStringList* leadingArgs)
{
    struct Cand { const char* name; QStringList args; };
    const QList<Cand> candidates = {{"python3", {}}, {"python", {}}, {"py", {QStringLiteral("-3")}}};
    for (const Cand& c : candidates) {
        const QString exe = QStandardPaths::findExecutable(QString::fromLatin1(c.name));
        if (exe.isEmpty()) continue;
        // The Windows Store "python" stub exists but fails: really run it.
        QProcess probe;
        QStringList args = c.args;
        args << QStringLiteral("-c") << QStringLiteral("import sys; sys.exit(0 if sys.version_info >= (3, 7) else 1)");
        probe.start(exe, args);
        if (probe.waitForFinished(4000) && probe.exitCode() == 0 && probe.exitStatus() == QProcess::NormalExit) {
            if (leadingArgs) *leadingArgs = c.args;
            return exe;
        }
    }
    return {};
}

bool LiveServer::start(const QString& root, int preferredPort, QString* error)
{
    if (m_running || m_starting) return true;
    const auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };

    QStringList lead;
    const QString python = findPython(&lead);
    if (python.isEmpty())
        return fail(tr("Python 3.7 or newer is needed for the live preview server, but it was not found.\n\n"
                       "Install it: Windows 'winget install Python.Python.3.12', macOS 'brew install python', "
                       "Ubuntu/Debian 'sudo apt install python3', Fedora 'sudo dnf install python3'."));

    // extract the bundled script
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/web");
    QDir().mkpath(dir);
    const QString script = dir + QStringLiteral("/live_server.py");
    QFile::remove(script);
    if (!QFile::copy(QStringLiteral(":/web/live_server.py"), script))
        return fail(tr("Could not unpack the live server script to %1").arg(script));
    QFile::setPermissions(script, QFile::permissions(script) | QFile::WriteOwner | QFile::ReadOwner);

    // first free port starting at the preferred one
    int port = 0;
    for (int p = preferredPort; p < preferredPort + 30 && port == 0; ++p) {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, quint16(p))) port = p;
    }
    if (port == 0) {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, 0)) port = probe.serverPort();
    }
    if (port == 0) return fail(tr("No free local port for the live server."));

    m_root = QDir::cleanPath(root);
    m_port = port;
    m_buffer.clear();
    m_starting = true;

    QStringList args = lead;
    args << script << QStringLiteral("--root") << m_root << QStringLiteral("--port") << QString::number(port)
         << QStringLiteral("--host") << QStringLiteral("127.0.0.1");
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_proc->setProcessEnvironment(env);
    m_proc->setWorkingDirectory(m_root);
    m_proc->start(python, args);
    m_startTimeout->start();
    emit message(tr("Starting live server for %1 ...").arg(QDir::toNativeSeparators(m_root)));
    emit stateChanged();
    return true;
}

void LiveServer::stop()
{
    m_startTimeout->stop();
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(1200)) m_proc->kill();
        m_proc->waitForFinished(1200);
    }
    if (m_running || m_starting) {
        m_running = m_starting = false;
        emit stateChanged();
    }
}

void LiveServer::handleLine(const QString& line)
{
    if (line.startsWith(QLatin1String("READY "))) {
        m_running = true;
        m_starting = false;
        m_startTimeout->stop();
        emit message(tr("Live server running at %1  (saving a file reloads the browser)").arg(line.mid(6)));
        emit stateChanged();
        emit ready(rootUrl());
    } else if (line.startsWith(QLatin1String("ERROR "))) {
        emit message(line.mid(6));
        stop();
    } else {
        emit message(line);
    }
}
