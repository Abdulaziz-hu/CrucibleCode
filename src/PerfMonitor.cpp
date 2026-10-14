#include "PerfMonitor.h"

#include <QDateTime>
#include <QFile>
#include <QThread>
#include <QThreadPool>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <psapi.h>
#  include <tlhelp32.h>
#elif defined(Q_OS_MACOS)
#  include <mach/mach.h>
#  include <mach/mach_host.h>
#  include <sys/sysctl.h>
#  include <unistd.h>
#else
#  include <unistd.h>
#endif

PerfMonitor::PerfMonitor(QObject* parent) : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &PerfMonitor::tick);
}

void PerfMonitor::setEnabled(bool on)
{
    if (on == m_timer.isActive()) return;
    if (on) { m_havePrev = false; tick(); m_timer.start(); }
    else m_timer.stop();
}

QString PerfMonitor::formatBytes(qint64 b)
{
    const double mb = double(b) / (1024.0 * 1024.0);
    return mb >= 1024 ? QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1) : QStringLiteral("%1 MB").arg(mb, 0, 'f', 0);
}

QString PerfMonitor::summary() const
{
    return QStringLiteral("CPU %1%  RAM %2  Threads %3")
        .arg(int(m_last.cpuTotal + 0.5)).arg(formatBytes(m_last.appMemory)).arg(m_last.processThreads);
}

QString PerfMonitor::details() const
{
    const Sample& s = m_last;
    return tr("CPU: %1% of the machine (%2 logical cores)\nCrucibleCode CPU: %3% of one core\n"
              "CrucibleCode memory: %4\nSystem memory: %5 of %6 used\n"
              "Threads: %7 in this process, %8 busy in the background thread pool")
        .arg(s.cpuTotal, 0, 'f', 1).arg(s.cores).arg(s.cpuApp * s.cores, 0, 'f', 1)
        .arg(formatBytes(s.appMemory)).arg(formatBytes(s.sysMemUsed), formatBytes(s.sysMemTotal))
        .arg(s.processThreads).arg(s.poolThreads);
}

void PerfMonitor::tick()
{
    Sample s;
    s.cores = qMax(1, QThread::idealThreadCount());
    s.poolThreads = QThreadPool::globalInstance()->activeThreadCount();
    const qint64 wall = QDateTime::currentMSecsSinceEpoch();
    quint64 idle = 0, total = 0, proc = 0;      // CPU time counters (any consistent unit)
    double procScale = 1.0;                      // converts `proc` delta to the unit of `total` delta

#if defined(Q_OS_LINUX)
    {
        QFile f(QStringLiteral("/proc/stat"));
        if (f.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> p = f.readLine().simplified().split(' ');       // cpu user nice system idle iowait irq softirq steal
            for (int i = 1; i < p.size() && i <= 8; ++i) {
                const quint64 v = p.at(i).toULongLong();
                total += v;
                if (i == 4 || i == 5) idle += v;
            }
        }
        QFile st(QStringLiteral("/proc/self/stat"));
        if (st.open(QIODevice::ReadOnly)) {
            const QByteArray all = st.readAll();
            const QList<QByteArray> p = all.mid(all.lastIndexOf(')') + 2).split(' ');   // fields after "(comm) "
            if (p.size() > 13) proc = p.at(11).toULongLong() + p.at(12).toULongLong();  // utime + stime
        }
        QFile status(QStringLiteral("/proc/self/status"));
        if (status.open(QIODevice::ReadOnly)) {
            for (const QByteArray& line : status.readAll().split('\n')) {
                if (line.startsWith("VmRSS:")) s.appMemory = line.mid(6).simplified().split(' ').value(0).toLongLong() * 1024;
                else if (line.startsWith("Threads:")) s.processThreads = line.mid(8).simplified().toInt();
            }
        }
        QFile mi(QStringLiteral("/proc/meminfo"));
        if (mi.open(QIODevice::ReadOnly)) {
            qint64 tot = 0, avail = 0;
            for (const QByteArray& line : mi.readAll().split('\n')) {
                if (line.startsWith("MemTotal:")) tot = line.mid(9).simplified().split(' ').value(0).toLongLong() * 1024;
                else if (line.startsWith("MemAvailable:")) avail = line.mid(13).simplified().split(' ').value(0).toLongLong() * 1024;
            }
            s.sysMemTotal = tot;
            s.sysMemUsed = tot - avail;
        }
        procScale = 1.0;   // both are in clock ticks (USER_HZ)
    }
#elif defined(Q_OS_WIN)
    {
        FILETIME i, k, u;
        if (GetSystemTimes(&i, &k, &u)) {
            const auto q = [](const FILETIME& f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
            idle = q(i);
            total = q(k) + q(u);           // kernel time includes idle time
        }
        FILETIME c, e, kk, uu;
        if (GetProcessTimes(GetCurrentProcess(), &c, &e, &kk, &uu)) {
            const auto q = [](const FILETIME& f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
            proc = q(kk) + q(uu);
        }
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) s.appMemory = qint64(pmc.WorkingSetSize);
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) {
            s.sysMemTotal = qint64(ms.ullTotalPhys);
            s.sysMemUsed = qint64(ms.ullTotalPhys - ms.ullAvailPhys);
        }
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te;
            te.dwSize = sizeof(te);
            const DWORD pid = GetCurrentProcessId();
            if (Thread32First(snap, &te)) do { if (te.th32OwnerProcessID == pid) ++s.processThreads; } while (Thread32Next(snap, &te));
            CloseHandle(snap);
        }
        procScale = 1.0;
    }
#elif defined(Q_OS_MACOS)
    {
        host_cpu_load_info_data_t load;
        mach_msg_type_number_t cnt = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&load), &cnt) == KERN_SUCCESS) {
            for (int i = 0; i < CPU_STATE_MAX; ++i) total += load.cpu_ticks[i];
            idle = load.cpu_ticks[CPU_STATE_IDLE];
        }
        task_basic_info_data_t info;
        mach_msg_type_number_t icnt = TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &icnt) == KERN_SUCCESS) {
            s.appMemory = qint64(info.resident_size);
            proc = quint64(info.user_time.seconds + info.system_time.seconds) * 1000000ull
                 + quint64(info.user_time.microseconds + info.system_time.microseconds);
        }
        thread_act_array_t threads;
        mach_msg_type_number_t tcount = 0;
        if (task_threads(mach_task_self(), &threads, &tcount) == KERN_SUCCESS) {
            s.processThreads = int(tcount);
            vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), tcount * sizeof(thread_t));
        }
        qint64 mem = 0;
        size_t len = sizeof(mem);
        sysctlbyname("hw.memsize", &mem, &len, nullptr, 0);
        s.sysMemTotal = mem;
        vm_statistics64_data_t vm;
        mach_msg_type_number_t vcnt = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &vcnt) == KERN_SUCCESS)
            s.sysMemUsed = qint64(vm.active_count + vm.wire_count + vm.compressor_page_count) * qint64(vm_page_size);
        procScale = 0;       // proc is in microseconds: handled through wall-clock below
    }
#endif

    if (m_havePrev) {
        const quint64 dTotal = total - m_prevTotal, dIdle = idle - m_prevIdle;
        if (dTotal > 0) s.cpuTotal = qBound(0.0, 100.0 * double(dTotal - dIdle) / double(dTotal), 100.0);
        if (procScale > 0 && dTotal > 0) {
#if defined(Q_OS_LINUX)
            s.cpuApp = double(proc - m_prevProc) / double(dTotal);            // fraction of all cores
#else
            s.cpuApp = double(proc - m_prevProc) / double(dTotal);
#endif
        } else if (wall > m_prevWall) {
            s.cpuApp = double(proc - m_prevProc) / 1000.0 / double(wall - m_prevWall) / s.cores;   // microseconds
        }
        s.cpuApp = qBound(0.0, s.cpuApp, 1.0);
    }
    m_prevIdle = idle;
    m_prevTotal = total;
    m_prevProc = proc;
    m_prevWall = wall;
    m_havePrev = true;
    m_last = s;
    emit updated();
}
