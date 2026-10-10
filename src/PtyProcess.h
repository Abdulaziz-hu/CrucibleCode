#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

// Runs a program attached to a pseudo terminal:
//   Linux / macOS : forkpty()
//   Windows       : ConPTY (Windows 10 1809 or newer)
class PtyProcess : public QObject {
    Q_OBJECT
public:
    explicit PtyProcess(QObject* parent = nullptr);
    ~PtyProcess() override;

    bool start(const QString& program, const QStringList& args, const QString& workingDir,
               int cols, int rows, QString* error = nullptr);
    void write(const QByteArray& data);
    void resize(int cols, int rows);
    bool isRunning() const;

signals:
    void dataReceived(const QByteArray& data);
    void exited(int exitCode);

private:
    void onReadable();

    struct Impl;
    Impl* d;
};
