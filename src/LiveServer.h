#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

class QProcess;
class QTimer;

// Local live-preview server for HTML / CSS / JS projects.
// Runs resources/web/live_server.py with the system Python: it serves the folder, injects a small
// script into pages and reloads the browser whenever a file is saved. Standard library only.
class LiveServer : public QObject {
    Q_OBJECT
public:
    explicit LiveServer(QObject* parent = nullptr);
    ~LiveServer() override;

    bool isRunning() const { return m_running; }
    bool isStarting() const { return m_starting; }
    int port() const { return m_port; }
    QString root() const { return m_root; }
    QUrl rootUrl() const;
    QUrl urlFor(const QString& filePath) const;   // page for an HTML file inside the root, otherwise the root

    bool start(const QString& root, int preferredPort, QString* error);
    void stop();

    // Python 3 interpreter: program + leading arguments (e.g. "py -3"). Empty program if none works.
    static QString findPython(QStringList* leadingArgs);

signals:
    void ready(const QUrl& url);
    void stateChanged();
    void message(const QString& text);

private:
    void handleLine(const QString& line);

    QProcess* m_proc = nullptr;
    QTimer* m_startTimeout = nullptr;
    QString m_root;
    QString m_buffer;
    int m_port = 0;
    bool m_running = false;
    bool m_starting = false;
};
