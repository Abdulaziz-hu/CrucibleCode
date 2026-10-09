#include "BuildManager.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

BuildManager::BuildManager(QObject* parent) : QObject(parent)
{
    m_proc = new QProcess(this);
    m_run = new QProcess(this);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] { consume(m_proc->readAllStandardOutput(), m_outBuf); });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] { consume(m_proc->readAllStandardError(), m_errBuf); });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        if (!m_outBuf.isEmpty()) { handleLine(m_outBuf); m_outBuf.clear(); }
        if (!m_errBuf.isEmpty()) { handleLine(m_errBuf); m_errBuf.clear(); }
        const bool ok = (st == QProcess::NormalExit && code == 0);
        if (!ok) {
            m_queue.clear();
            m_building = false;
            emit finished(code, false);
            return;
        }
        if (!m_queue.isEmpty()) { startNext(); return; }
        m_building = false;
        emit finished(0, true);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit message(tr("Failed to start '%1'. Set the CMake path in Tools > Settings.").arg(m_proc->program()), Error);
            m_queue.clear();
            m_building = false;
            emit finished(-1, false);
        }
    });

    const auto runOut = [this](bool err) {
        QString& buf = err ? m_runErrBuf : m_runOutBuf;
        buf += QString::fromLocal8Bit(err ? m_run->readAllStandardError() : m_run->readAllStandardOutput());
        int nl;
        while ((nl = int(buf.indexOf(QLatin1Char('\n')))) >= 0) {
            QString line = buf.left(nl);
            buf.remove(0, nl + 1);
            if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
            emit message(line, Normal);
        }
    };
    connect(m_run, &QProcess::readyReadStandardOutput, this, [runOut] { runOut(false); });
    connect(m_run, &QProcess::readyReadStandardError, this, [runOut] { runOut(true); });
    connect(m_run, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        if (!m_runOutBuf.isEmpty()) { emit message(m_runOutBuf, Normal); m_runOutBuf.clear(); }
        if (!m_runErrBuf.isEmpty()) { emit message(m_runErrBuf, Normal); m_runErrBuf.clear(); }
        const bool crashed = st == QProcess::CrashExit;
        emit message(crashed ? tr("Process crashed.") : tr("Process exited with code %1.").arg(code),
                     (crashed || code != 0) ? Warning : Info);
        emit finished(code, !crashed && code == 0);
    });
    connect(m_run, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit message(tr("Failed to start '%1'.").arg(m_run->program()), Error);
            emit finished(-1, false);
        }
    });
}

void BuildManager::setProjectRoot(const QString& root)
{
    m_root = QDir::cleanPath(root);
}

bool BuildManager::hasProject() const
{
    return !m_root.isEmpty() && QFileInfo::exists(m_root + QStringLiteral("/CMakeLists.txt"));
}

QString BuildManager::buildRoot() const { return m_root + QStringLiteral("/build"); }

QString BuildManager::buildDir() const
{
    return buildRoot() + QLatin1Char('/') + m_config.toLower();
}

bool BuildManager::isBusy() const
{
    return m_building || m_run->state() != QProcess::NotRunning;
}

bool BuildManager::needsConfigure() const
{
    return !QFileInfo::exists(buildDir() + QStringLiteral("/CMakeCache.txt"));
}

BuildManager::Step BuildManager::configureStep() const
{
    Step s;
    s.title = tr("Configuring (%1)...").arg(m_config);
    s.program = m_cmake;
    s.args = {QStringLiteral("-S"), m_root, QStringLiteral("-B"), buildDir(),
              QStringLiteral("-DCMAKE_BUILD_TYPE=") + m_config,
              QStringLiteral("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"),
              QStringLiteral("-DCMAKE_COLOR_DIAGNOSTICS=OFF")};
    if (!m_generator.isEmpty())
        s.args << QStringLiteral("-G") << m_generator;
    if (!m_extraArgs.trimmed().isEmpty())
        s.args << QProcess::splitCommand(m_extraArgs);
    return s;
}

BuildManager::Step BuildManager::buildStep(const QString& target, bool cleanFirst) const
{
    Step s;
    s.title = target == QLatin1String("clean") ? tr("Cleaning (%1)...").arg(m_config)
                                               : tr("Building (%1)...").arg(m_config);
    s.program = m_cmake;
    s.args = {QStringLiteral("--build"), buildDir(), QStringLiteral("--config"), m_config};
    if (!target.isEmpty()) s.args << QStringLiteral("--target") << target;
    if (cleanFirst) s.args << QStringLiteral("--clean-first");
    s.args << QStringLiteral("--parallel");
    return s;
}

void BuildManager::begin(const QList<Step>& steps)
{
    if (m_building) return;
    if (!hasProject()) {
        emit message(tr("No CMakeLists.txt found in the opened folder."), Error);
        emit finished(-1, false);
        return;
    }
    m_queue.clear();
    for (const Step& s : steps) m_queue.enqueue(s);
    m_building = true;
    m_hadErrors = false;
    emit started(steps.isEmpty() ? QString() : steps.last().title);
    startNext();
}

void BuildManager::startNext()
{
    const Step s = m_queue.dequeue();
    emit message(QStringLiteral("> ") + s.program + QLatin1Char(' ') + s.args.join(QLatin1Char(' ')), Info);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_MESSAGES"), QStringLiteral("C"));  // stable, parseable messages
    env.insert(QStringLiteral("VSLANG"), QStringLiteral("1033"));
    m_proc->setProcessEnvironment(env);
    m_proc->setWorkingDirectory(m_root);
    m_proc->start(s.program, s.args);
}

void BuildManager::configure() { begin({configureStep()}); }

void BuildManager::build()
{
    QList<Step> steps;
    if (needsConfigure()) steps << configureStep();
    steps << buildStep();
    begin(steps);
}

void BuildManager::rebuild()
{
    QList<Step> steps;
    if (needsConfigure()) steps << configureStep();
    steps << buildStep(QString(), true);
    begin(steps);
}

void BuildManager::clean()
{
    if (needsConfigure()) {
        emit message(tr("Nothing to clean: the project has not been configured yet."), Info);
        return;
    }
    begin({buildStep(QStringLiteral("clean"))});
}

void BuildManager::stop()
{
    m_queue.clear();
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        emit message(tr("Build cancelled."), Warning);
    }
    if (m_run->state() != QProcess::NotRunning) {
        m_run->kill();
    }
}

void BuildManager::run(const QString& executable)
{
    if (m_run->state() != QProcess::NotRunning) return;
    if (!QFileInfo::exists(executable)) {
        emit message(tr("Executable not found: %1").arg(executable), Error);
        return;
    }
    emit message(tr("Running %1").arg(executable), Info);
    m_run->setWorkingDirectory(QFileInfo(executable).absolutePath());
    m_run->start(executable, {});
}

QStringList BuildManager::findExecutables() const
{
    QStringList result;
    const QString root = buildDir();
    if (!QFileInfo::exists(root)) return result;

    static const QStringList skipSuffix = {"so", "dylib", "a", "lib", "dll", "cmake", "sh", "py",
                                           "o", "obj", "pdb", "ilk", "exp", "txt", "json", "pc"};
    const auto scan = [&](auto&& self, const QString& dir, int depth) -> void {
        if (depth > 4) return;
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : entries) {
            if (fi.isDir()) {
                const QString n = fi.fileName();
                if (n == QLatin1String("CMakeFiles") || n == QLatin1String("_deps")
                    || n.endsWith(QLatin1String(".dir")) || n.endsWith(QLatin1String(".app")))
                    continue;
                self(self, fi.absoluteFilePath(), depth + 1);
            } else if (fi.isExecutable() && !skipSuffix.contains(fi.suffix().toLower())
                       && !fi.fileName().contains(QLatin1String(".so."))) {
#ifdef Q_OS_WIN
                if (fi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0) continue;
#endif
                result << fi.absoluteFilePath();
            }
        }
    };
    scan(scan, root, 0);
    result.sort();
    return result;
}

// ---- output handling ---------------------------------------------------------------------

void BuildManager::consume(const QByteArray& data, QString& buffer)
{
    buffer += QString::fromLocal8Bit(data);
    int nl;
    while ((nl = int(buffer.indexOf(QLatin1Char('\n')))) >= 0) {
        const QString line = buffer.left(nl);
        buffer.remove(0, nl + 1);
        handleLine(line);
    }
}

void BuildManager::handleLine(QString line)
{
    static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;]*[A-Za-z]"));
    line.remove(ansi);
    if (line.endsWith(QLatin1Char('\r'))) line.chop(1);

    Diagnostic d;
    Kind kind = Normal;
    if (parseDiagnostic(line, &d)) {
        d.file = resolvePath(d.file);
        kind = d.severity == Diagnostic::Error ? Error
             : d.severity == Diagnostic::Warning ? Warning : Normal;
        if (d.severity != Diagnostic::Note) emit diagnostic(d);
        if (d.severity == Diagnostic::Error) m_hadErrors = true;
    }
    emit message(line, kind);
}

QString BuildManager::resolvePath(const QString& file) const
{
    if (file.isEmpty()) return file;
    const QFileInfo direct(file);
    if (direct.isAbsolute()) return direct.exists() ? direct.canonicalFilePath() : file;
    for (const QString& base : {buildDir(), m_root}) {
        const QFileInfo fi(QDir(base).filePath(file));
        if (fi.exists()) return fi.canonicalFilePath();
    }
    return file;
}

bool BuildManager::parseDiagnostic(const QString& line, Diagnostic* out)
{
    // GCC / Clang:  path:line:col: error: message
    static const QRegularExpression gcc(QStringLiteral(
        "^(.+?):(\\d+):(?:(\\d+):)?\\s*(fatal error|error|warning|note):\\s*(.*)$"));
    // MSVC:  path(line[,col]): error C1234: message
    static const QRegularExpression msvc(QStringLiteral(
        "^(?:\\s*\\d+>)?(.+?)\\((\\d+)(?:,(\\d+))?\\)\\s*:\\s*(fatal error|error|warning)\\s+([A-Z]+\\d+)\\s*:\\s*(.*)$"));
    // CMake:  CMake Error at CMakeLists.txt:12 (find_package):
    static const QRegularExpression cmake(QStringLiteral(
        "^CMake (Error|Warning)(?: \\(dev\\))? at (.+?):(\\d+) \\((.*)\\):?$"));

    const auto sev = [](const QString& s) {
        return s.contains(QLatin1String("error")) ? Diagnostic::Error
             : s == QLatin1String("warning") ? Diagnostic::Warning : Diagnostic::Note;
    };

    if (auto m = gcc.match(line); m.hasMatch()) {
        out->file = m.captured(1);
        out->line = m.captured(2).toInt();
        out->column = m.captured(3).toInt();
        out->severity = sev(m.captured(4));
        out->message = m.captured(5);
        return true;
    }
    if (auto m = msvc.match(line); m.hasMatch()) {
        out->file = m.captured(1).trimmed();
        out->line = m.captured(2).toInt();
        out->column = m.captured(3).toInt();
        out->severity = sev(m.captured(4));
        out->message = m.captured(5) + QStringLiteral(": ") + m.captured(6);
        return true;
    }
    if (auto m = cmake.match(line); m.hasMatch()) {
        out->file = m.captured(2);
        out->line = m.captured(3).toInt();
        out->column = 0;
        out->severity = m.captured(1) == QLatin1String("Error") ? Diagnostic::Error : Diagnostic::Warning;
        out->message = QStringLiteral("CMake: ") + m.captured(4);
        return true;
    }
    if (line.contains(QLatin1String("undefined reference to"))
        || line.startsWith(QLatin1String("ninja: error"))
        || line.startsWith(QLatin1String("CMake Error"))
        || line.contains(QLatin1String("LINK : fatal error"))
        || line.contains(QLatin1String("error LNK"))) {
        out->file.clear();
        out->line = 0;
        out->column = 0;
        out->severity = Diagnostic::Error;
        out->message = line.trimmed();
        return true;
    }
    return false;
}
