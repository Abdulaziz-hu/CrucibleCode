#include "PtyProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
// ===================================================================================
//  Windows: ConPTY
// ===================================================================================
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <thread>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

// ConPTY entry points are resolved at runtime from kernel32.dll. This avoids depending on
// the SDK / MinGW headers declaring them (older MinGW-w64 hides them behind NTDDI checks)
// and lets the app run (without a terminal) on Windows older than 10 1809.
namespace {
using PtyHpc = void*;
using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, PtyHpc*);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(PtyHpc, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(PtyHpc);

struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
    bool ok() const { return create && resize && close; }
};

template <typename Fn>
Fn loadKernel32(const char* name)
{
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    if (!k) return nullptr;
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(k, name)));
}

const ConPtyApi& conpty()
{
    static const ConPtyApi api = [] {
        ConPtyApi a;
        a.create = loadKernel32<CreatePseudoConsoleFn>("CreatePseudoConsole");
        a.resize = loadKernel32<ResizePseudoConsoleFn>("ResizePseudoConsole");
        a.close = loadKernel32<ClosePseudoConsoleFn>("ClosePseudoConsole");
        return a;
    }();
    return api;
}
} // namespace

struct PtyProcess::Impl {
    PtyHpc hpc = nullptr;
    HANDLE inWrite = nullptr;
    HANDLE outRead = nullptr;
    HANDLE proc = nullptr;
    std::thread reader;
    std::atomic<bool> stopping{false};
    bool running = false;
};

PtyProcess::PtyProcess(QObject* parent) : QObject(parent), d(new Impl) {}

PtyProcess::~PtyProcess()
{
    d->stopping = true;
    if (d->proc) TerminateProcess(d->proc, 1);
    if (d->hpc && conpty().close) conpty().close(d->hpc);
    if (d->inWrite) CloseHandle(d->inWrite);
    if (d->reader.joinable()) d->reader.join();
    if (d->outRead) CloseHandle(d->outRead);
    if (d->proc) CloseHandle(d->proc);
    delete d;
}

static QString quoteArg(const QString& s)
{
    if (!s.isEmpty() && !s.contains(QLatin1Char(' ')) && !s.contains(QLatin1Char('"'))) return s;
    QString r = s;
    r.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + r + QLatin1Char('"');
}

bool PtyProcess::start(const QString& program, const QStringList& args, const QString& workingDir,
                       int cols, int rows, QString* error)
{
    const auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };

    QString path = program;
    if (!QFileInfo(program).isAbsolute()) path = QStandardPaths::findExecutable(program);
    if (path.isEmpty()) return fail(QStringLiteral("Program not found: %1").arg(program));
    path = QDir::toNativeSeparators(path);

    HANDLE inRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &d->inWrite, nullptr, 0) || !CreatePipe(&d->outRead, &outWrite, nullptr, 0))
        return fail(QStringLiteral("CreatePipe failed (%1)").arg(GetLastError()));

    COORD size;
    size.X = SHORT(cols);
    size.Y = SHORT(rows);
    if (!conpty().ok()) {
        CloseHandle(inRead);
        CloseHandle(outWrite);
        return fail(QStringLiteral("ConPTY is not available (needs Windows 10 version 1809 or newer)"));
    }
    const HRESULT hr = conpty().create(size, inRead, outWrite, 0, &d->hpc);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (FAILED(hr))
        return fail(QStringLiteral("CreatePseudoConsole failed (needs Windows 10 version 1809 or newer)"));

    STARTUPINFOEXW si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    si.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attrSize));
    if (!si.lpAttributeList || !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize))
        return fail(QStringLiteral("InitializeProcThreadAttributeList failed"));
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              d->hpc, sizeof(PtyHpc), nullptr, nullptr);

    QString cmd = quoteArg(path);
    for (const QString& a : args) cmd += QLatin1Char(' ') + quoteArg(a);
    std::wstring wcmd = cmd.toStdWString();
    std::vector<wchar_t> cmdBuf(wcmd.begin(), wcmd.end());
    cmdBuf.push_back(L'\0');
    const std::wstring wcwd = QDir::toNativeSeparators(workingDir).toStdWString();

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    const BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                                   EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                   wcwd.empty() ? nullptr : wcwd.c_str(), &si.StartupInfo, &pi);
    const DWORD lastErr = GetLastError();
    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    if (!ok) return fail(QStringLiteral("CreateProcess failed (%1)").arg(lastErr));

    CloseHandle(pi.hThread);
    d->proc = pi.hProcess;
    d->running = true;

    d->reader = std::thread([this] {
        char buf[8192];
        DWORD n = 0;
        while (ReadFile(d->outRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
            if (d->stopping) return;
            const QByteArray chunk(buf, int(n));
            QMetaObject::invokeMethod(this, [this, chunk] { emit dataReceived(chunk); }, Qt::QueuedConnection);
        }
        if (d->stopping) return;
        DWORD code = 0;
        WaitForSingleObject(d->proc, 2000);
        GetExitCodeProcess(d->proc, &code);
        QMetaObject::invokeMethod(this, [this, code] {
            d->running = false;
            emit exited(int(code));
        }, Qt::QueuedConnection);
    });
    return true;
}

void PtyProcess::write(const QByteArray& data)
{
    if (!d->running || !d->inWrite) return;
    DWORD written = 0;
    WriteFile(d->inWrite, data.constData(), DWORD(data.size()), &written, nullptr);
}

void PtyProcess::resize(int cols, int rows)
{
    if (!d->hpc || !conpty().resize) return;
    COORD size;
    size.X = SHORT(cols);
    size.Y = SHORT(rows);
    conpty().resize(d->hpc, size);
}

bool PtyProcess::isRunning() const { return d->running; }
void PtyProcess::onReadable() {}

#else
// ===================================================================================
//  Linux / macOS: forkpty
// ===================================================================================
#include <QSocketNotifier>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#ifdef Q_OS_DARWIN
#include <util.h>
#else
#include <pty.h>
#endif

struct PtyProcess::Impl {
    int fd = -1;
    pid_t pid = -1;
    bool running = false;
    QSocketNotifier* notifier = nullptr;
};

PtyProcess::PtyProcess(QObject* parent) : QObject(parent), d(new Impl) {}

PtyProcess::~PtyProcess()
{
    if (d->notifier) d->notifier->setEnabled(false);
    if (d->running && d->pid > 0) {
        ::killpg(d->pid, SIGHUP);
        ::kill(d->pid, SIGKILL);
    }
    if (d->fd >= 0) ::close(d->fd);
    if (d->pid > 0) {
        int st = 0;
        ::waitpid(d->pid, &st, 0);
    }
    delete d;
}

bool PtyProcess::start(const QString& program, const QStringList& args, const QString& workingDir,
                       int cols, int rows, QString* error)
{
    const auto fail = [&](const QString& msg) {
        if (error) *error = msg;
        return false;
    };

    QString path = program;
    if (!QFileInfo(program).isAbsolute()) path = QStandardPaths::findExecutable(program);
    if (path.isEmpty()) return fail(QStringLiteral("Program not found: %1").arg(program));

    // Everything the child needs is prepared before fork(): no allocation afterwards.
    const QByteArray pathB = QFile::encodeName(path);
    QList<QByteArray> argStore;
    argStore << QFile::encodeName(QFileInfo(path).fileName());
    for (const QString& a : args) argStore << a.toLocal8Bit();
    std::vector<char*> argv;
    for (QByteArray& a : argStore) argv.push_back(a.data());
    argv.push_back(nullptr);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    env.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    env.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("CrucibleCode"));
    QList<QByteArray> envStore;
    const QStringList keys = env.keys();
    for (const QString& k : keys)
        envStore << (k + QLatin1Char('=') + env.value(k)).toLocal8Bit();
    std::vector<char*> envp;
    for (QByteArray& e : envStore) envp.push_back(e.data());
    envp.push_back(nullptr);

    const QByteArray cwd = QFile::encodeName(workingDir);

    struct winsize ws;
    ::memset(&ws, 0, sizeof(ws));
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);

    int fd = -1;
    const pid_t pid = ::forkpty(&fd, nullptr, nullptr, &ws);
    if (pid < 0) return fail(QStringLiteral("forkpty failed: %1").arg(QString::fromLocal8Bit(::strerror(errno))));
    if (pid == 0) {
        if (!cwd.isEmpty()) {
            if (::chdir(cwd.constData()) != 0) { /* keep the inherited directory */ }
        }
        ::execve(pathB.constData(), argv.data(), envp.data());
        ::_exit(127);
    }

    d->fd = fd;
    d->pid = pid;
    d->running = true;
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);

    d->notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(d->notifier, &QSocketNotifier::activated, this, [this] { onReadable(); });
    return true;
}

void PtyProcess::onReadable()
{
    char buf[8192];
    for (;;) {
        const ssize_t n = ::read(d->fd, buf, sizeof(buf));
        if (n > 0) {
            emit dataReceived(QByteArray(buf, int(n)));
            if (!d->running) return;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        break; // EOF / EIO: the child side is closed
    }

    d->notifier->setEnabled(false);
    int status = 0, code = -1;
    if (d->pid > 0 && ::waitpid(d->pid, &status, 0) > 0)
        code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    d->pid = -1;
    d->running = false;
    emit exited(code);
}

void PtyProcess::write(const QByteArray& data)
{
    if (!d->running) return;
    const char* p = data.constData();
    qsizetype left = data.size();
    while (left > 0) {
        const ssize_t n = ::write(d->fd, p, size_t(left));
        if (n > 0) { p += n; left -= n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) {
            struct pollfd pf;
            pf.fd = d->fd;
            pf.events = POLLOUT;
            pf.revents = 0;
            if (::poll(&pf, 1, 200) <= 0) break;
            continue;
        }
        break;
    }
}

void PtyProcess::resize(int cols, int rows)
{
    if (d->fd < 0) return;
    struct winsize ws;
    ::memset(&ws, 0, sizeof(ws));
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    ::ioctl(d->fd, TIOCSWINSZ, &ws);
}

bool PtyProcess::isRunning() const { return d->running; }

#endif

