#pragma once
#include "LspTypes.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <functional>

// One Language Server process spoken to over stdio (JSON-RPC 2.0 with Content-Length framing).
class LspClient : public QObject {
    Q_OBJECT
public:
    enum State { Stopped, Starting, Ready, Failed };
    using Callback = std::function<void(const QJsonValue& result, const QJsonObject& error)>;

    LspClient(const QString& name, const QString& command, const QStringList& args, const QString& rootPath, QObject* parent = nullptr);
    ~LspClient() override;

    void start();
    void shutdown();                       // polite: shutdown + exit, then kill after a moment
    State state() const { return m_state; }
    QString name() const { return m_name; }
    QString errorText() const { return m_error; }
    QString stderrTail() const { return QString::fromUtf8(m_stderr); }

    static QString toUri(const QString& path);
    static QString fromUri(const QString& uri);

    // documents
    void didOpen(const QString& uri, const QString& languageId, int version, const QString& text);
    void didChange(const QString& uri, int version, const QString& text);
    void didSave(const QString& uri);
    void didClose(const QString& uri);

    int request(const QString& method, const QJsonObject& params, Callback cb);
    void notify(const QString& method, const QJsonObject& params);

    // convenience wrappers that parse the answers
    void completion(const QString& uri, int line, int character, std::function<void(const QList<LspCompletionItem>&)> cb);
    void definition(const QString& uri, int line, int character, std::function<void(const QString& path, int line, int col)> cb);
    void hover(const QString& uri, int line, int character, std::function<void(const QString& text)> cb);

signals:
    void stateChanged();
    void diagnosticsReceived(const QString& uri, const QList<LspDiagnostic>& list);
    void logMessage(const QString& text);

private:
    void send(const QJsonObject& msg);
    void onData();
    void handle(const QJsonObject& msg);
    void setState(State s, const QString& error = QString());
    void sendInitialize();

    QString m_name, m_command, m_root;
    QStringList m_args;
    QProcess m_proc;
    QByteArray m_buf, m_stderr;
    State m_state = Stopped;
    QString m_error;
    int m_nextId = 1;
    QHash<int, Callback> m_pending;
    bool m_shuttingDown = false;
};
