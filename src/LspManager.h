#pragma once
#include "LspClient.h"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <functional>

class CodeEditor;
class QTimer;

struct LspServerConfig {
    QString name;               // "clangd"
    QStringList extensions;     // "cpp", "h", ...
    QString command;
    QStringList args;
    bool enabled = true;
};

// Starts language servers on demand, keeps their documents in sync with the editors and routes
// diagnostics, completion, hover and go-to-definition between servers and editors.
class LspManager : public QObject {
    Q_OBJECT
public:
    explicit LspManager(QObject* parent = nullptr);
    ~LspManager() override;

    static QList<LspServerConfig> defaultServers();
    static QList<LspServerConfig> loadServers();
    static void saveServers(const QList<LspServerConfig>& list);
    static bool globalEnabled();
    static void setGlobalEnabled(bool on);
    static bool commandAvailable(const QString& command);

    void setProjectRoot(const QString& root);      // restarts the servers
    void attach(CodeEditor* e);                    // call after a file has been loaded
    void detach(CodeEditor* e);
    void notifySaved(CodeEditor* e);
    void reloadSettings();                         // after the settings dialog: restart everything

    void complete(CodeEditor* e, bool manual);
    void gotoDefinition(CodeEditor* e);
    void hover(CodeEditor* e);

    QString statusText() const;                    // empty when no server is involved
    QString statusTooltip() const;
    QMap<QString, QList<LspDiagnostic>> problems() const { return m_problems; }   // absolute path -> diagnostics
    int count(int severity) const;

    void shutdownAll();

signals:
    void statusChanged();
    void problemsChanged();
    void locationRequested(const QString& path, int line, int column);
    void message(const QString& text);

private:
    struct Doc {
        QPointer<CodeEditor> editor;
        QString path, uri, languageId;
        LspClient* client = nullptr;
        QString serverName;
        int version = 1;
        bool opened = false;
        QTimer* timer = nullptr;
    };
    LspServerConfig configFor(const QString& path) const;
    LspClient* clientFor(const LspServerConfig& cfg);
    void flush(Doc& d);
    void clientStateChanged(LspClient* c);
    Doc* docFor(CodeEditor* e);
    static QString languageIdFor(const QString& path);
    QString findCompileCommandsDir() const;

    QString m_root;
    QHash<QString, LspClient*> m_clients;          // server name -> client
    QHash<QString, QString> m_unavailable;         // server name -> reason
    QHash<CodeEditor*, Doc> m_docs;
    QMap<QString, QList<LspDiagnostic>> m_problems;
};
