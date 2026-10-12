#pragma once
#include <QList>
#include <functional>
#include <QObject>
#include <QPointer>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

// Optional local "AI tab completion".
//   * runtime : llama.cpp's `llama-server` (MIT). Taken from your PATH, a path you choose, or downloaded
//               from the official GitHub releases into the app-data folder.
//   * model   : a small code model in GGUF format (default: Qwen2.5-Coder 0.5B, Apache-2.0) downloaded from
//               Hugging Face, or any GGUF you point to.
// Everything runs on this computer; nothing you type is sent anywhere. The only network traffic is the
// one-time download you start yourself.
class AiCompletion : public QObject {
    Q_OBJECT
public:
    struct ModelPreset {
        QString id;
        QString title;
        QString url;
        QString fileName;
    };
    enum State { Disabled, NotReady, Starting, Ready, Failed };

    static QList<ModelPreset> presets();

    explicit AiCompletion(QObject* parent = nullptr);
    ~AiCompletion() override;

    // settings
    static bool enabledInSettings();
    QString dataDir() const;
    QString runtimePath() const;     // llama-server executable, empty if none
    QString modelPath() const;       // GGUF file, empty if none
    bool runtimeAvailable() const { return !runtimePath().isEmpty(); }
    bool modelAvailable() const { return !modelPath().isEmpty(); }

    State state() const { return m_state; }
    QString statusText() const;
    bool isReady() const { return m_state == Ready; }
    bool isDownloading() const { return m_reply != nullptr; }

    quint64 requestCompletion(const QString& prefix, const QString& suffix);

public slots:
    void applySettings();            // starts or stops the server according to the settings
    void start();
    void stop();
    void downloadRuntime();
    void downloadModel();
    void cancelDownload();
    void removeDownloads();

signals:
    void stateChanged();
    void progress(const QString& what, qint64 received, qint64 total);
    void message(const QString& text);
    void downloadFinished(bool ok, const QString& text);
    void completionReady(quint64 id, const QString& text);

private:
    void setState(State s, const QString& detail = {});
    void download(const QUrl& url, const QString& destFile, const QString& what, std::function<void(bool, QString)> done);
    void extractRuntime(const QString& archive);
    QString locateServer() const;
    void pollHealth();

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;      // the active download
    QNetworkReply* m_request = nullptr;    // the active completion request
    QProcess* m_server = nullptr;
    QTimer* m_health = nullptr;
    QFile* m_out = nullptr;
    State m_state = Disabled;
    QString m_detail;
    int m_port = 0;
    int m_healthTries = 0;
    quint64 m_nextId = 1;
    quint64 m_activeId = 0;
};
