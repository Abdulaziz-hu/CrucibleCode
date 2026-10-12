#include "RunHelper.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace RunHelper {

namespace {

enum class ShellKind { Posix, PowerShell, Cmd };

ShellKind shellKind(const QString& shellProgram)
{
    const QString n = QFileInfo(shellProgram).completeBaseName().toLower();
    if (n == QLatin1String("pwsh") || n == QLatin1String("powershell")) return ShellKind::PowerShell;
    if (n == QLatin1String("cmd")) return ShellKind::Cmd;
    return ShellKind::Posix;
}

QString quote(const QString& s, ShellKind k)
{
    switch (k) {
    case ShellKind::PowerShell: {
        QString r = s;
        r.replace(QLatin1Char('\''), QStringLiteral("''"));
        return QLatin1Char('\'') + r + QLatin1Char('\'');
    }
    case ShellKind::Cmd:
        return QLatin1Char('"') + s + QLatin1Char('"');
    case ShellKind::Posix:
    default: {
        QString r = s;
        r.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return QLatin1Char('\'') + r + QLatin1Char('\'');
    }
    }
}

struct Interp {
    QStringList candidates;   // executables to look for, first found wins
    QStringList args;         // arguments placed before the file
};

bool lookup(const QString& ext, Interp* out)
{
    static const QHash<QString, Interp> table = {
        {"sh",   {{"bash", "sh"}, {}}},
        {"bash", {{"bash"}, {}}},
        {"zsh",  {{"zsh"}, {}}},
        {"fish", {{"fish"}, {}}},
        {"ps1",  {{"pwsh", "powershell"}, {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File"}}},
        {"py",   {{"python3", "python", "py"}, {}}},
        {"pyw",  {{"python3", "python", "py"}, {}}},
        {"js",   {{"node"}, {}}},
        {"mjs",  {{"node"}, {}}},
        {"cjs",  {{"node"}, {}}},
        {"rb",   {{"ruby"}, {}}},
        {"pl",   {{"perl"}, {}}},
        {"php",  {{"php"}, {}}},
        {"lua",  {{"lua", "lua5.4"}, {}}},
        {"r",    {{"Rscript"}, {}}},
        {"jar",  {{"java"}, {"-jar"}}},
        {"go",   {{"go"}, {"run"}}},
        {"swift", {{"swift"}, {}}},
        {"groovy", {{"groovy"}, {}}},
    };
    const auto it = table.constFind(ext);
    if (it == table.constEnd()) return false;
    *out = it.value();
    return true;
}

QString shebangInterpreter(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray first = f.readLine(256);
    if (!first.startsWith("#!")) return {};
    QStringList parts = QString::fromUtf8(first.mid(2)).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return {};
    QString exe = QFileInfo(parts.first()).fileName();
    if (exe == QLatin1String("env") && parts.size() > 1) exe = parts.at(1);
    return exe;
}

} // namespace

bool isRunnable(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.isFile()) return false;
    Interp i;
    const QString ext = fi.suffix().toLower();
    if (lookup(ext, &i)) return true;
#ifdef Q_OS_WIN
    return ext == QLatin1String("exe") || ext == QLatin1String("bat") || ext == QLatin1String("cmd")
        || ext == QLatin1String("com");
#else
    if (ext.isEmpty() && !shebangInterpreter(path).isEmpty()) return true;
    return fi.isExecutable() && ext != QLatin1String("txt") && ext != QLatin1String("md");
#endif
}

QString commandFor(const QString& path, const QString& shellProgram, QString* error)
{
    const QFileInfo fi(path);
    const ShellKind k = shellKind(shellProgram);
    const QString ext = fi.suffix().toLower();
    const QString nativePath = QDir::toNativeSeparators(fi.absoluteFilePath());

    const auto fail = [&](const QString& msg) { if (error) *error = msg; return QString(); };

    // A command word: bare name when it is on PATH, otherwise an absolute path.
    const auto invoke = [&](const QString& exe, bool absolute) {
        if (!absolute) return exe;
        return k == ShellKind::PowerShell ? QStringLiteral("& ") + quote(exe, k) : quote(exe, k);
    };

    Interp spec;
    QString interpName;
    if (lookup(ext, &spec)) {
        interpName = spec.candidates.first();
    } else if (ext.isEmpty()) {
        const QString sb = shebangInterpreter(path);
        if (!sb.isEmpty()) { spec.candidates = {sb}; interpName = sb; }
    }

    if (!spec.candidates.isEmpty()) {
        QString found, foundPath;
        for (const QString& c : std::as_const(spec.candidates)) {
            const QString p = QStandardPaths::findExecutable(c);
            if (!p.isEmpty()) { found = c; foundPath = p; break; }
        }
#ifdef Q_OS_WIN
        if (found.isEmpty() && (ext == QLatin1String("sh") || ext == QLatin1String("bash"))) {
            for (const QString& base : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)"),
                                        qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs")}) {
                const QString b = base + QStringLiteral("/Git/bin/bash.exe");
                if (!base.isEmpty() && QFileInfo::exists(b)) { found = b; foundPath = b; break; }
            }
        }
#endif
        if (found.isEmpty())
            return fail(QObject::tr("'%1' was not found in PATH, so this file cannot be run. Install it and try again.")
                            .arg(spec.candidates.join(QStringLiteral(" / "))));

        const bool absolute = QFileInfo(found).isAbsolute();
        QString cmd = invoke(absolute ? foundPath : found, absolute);
        if (found == QLatin1String("py")) cmd += QStringLiteral(" -3");
        for (const QString& a : std::as_const(spec.args)) cmd += QLatin1Char(' ') + a;
        return cmd + QLatin1Char(' ') + quote(nativePath, k);
    }

    // executables and batch files
#ifdef Q_OS_WIN
    if (ext == QLatin1String("bat") || ext == QLatin1String("cmd"))
        return k == ShellKind::PowerShell ? QStringLiteral("& ") + quote(nativePath, k) : quote(nativePath, k);
    if (ext == QLatin1String("exe") || ext == QLatin1String("com"))
        return k == ShellKind::PowerShell ? QStringLiteral("& ") + quote(nativePath, k) : quote(nativePath, k);
#else
    if (fi.isExecutable()) return quote(nativePath, k);
#endif
    return fail(QObject::tr("Do not know how to run '%1'.").arg(fi.fileName()));
}

} // namespace RunHelper
