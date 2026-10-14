#pragma once
#include <QObject>
#include <QString>
#include <QTimer>

// Samples CPU / memory / thread counts once per second while running. Linux reads /proc, Windows
// uses GetSystemTimes / GetProcessMemoryInfo / Toolhelp, macOS uses Mach APIs.
class PerfMonitor : public QObject {
    Q_OBJECT
public:
    struct Sample {
        double cpuTotal = 0;       // whole machine, %
        double cpuApp = 0;         // this process, % of one machine (all cores = 100)
        qint64 appMemory = 0;      // resident set of this process, bytes
        qint64 sysMemUsed = 0;     // bytes
        qint64 sysMemTotal = 0;    // bytes
        int processThreads = 0;    // OS threads of this process
        int poolThreads = 0;       // QThreadPool threads that are currently running work
        int cores = 1;
    };

    explicit PerfMonitor(QObject* parent = nullptr);
    void setEnabled(bool on);
    bool isEnabled() const { return m_timer.isActive(); }
    const Sample& last() const { return m_last; }
    QString summary() const;       // status bar text
    QString details() const;       // tooltip

    static QString formatBytes(qint64 b);

signals:
    void updated();

private:
    void tick();
    Sample m_last;
    QTimer m_timer;
    // previous counters for delta computations
    quint64 m_prevIdle = 0, m_prevTotal = 0, m_prevProc = 0;
    qint64 m_prevWall = 0;
    bool m_havePrev = false;
};
