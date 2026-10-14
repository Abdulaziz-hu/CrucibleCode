#include "LspClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

LspClient::LspClient(const QString& name, const QString& command, const QStringList& args, const QString& rootPath, QObject* parent)
    : QObject(parent), m_name(name), m_command(command), m_root(rootPath), m_args(args)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &LspClient::onData);
    connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_proc.readAllStandardError();          // kept for diagnosing start-up failures, not shown
        if (m_stderr.size() > 8192) m_stderr = m_stderr.right(8192);
    });
    connect(&m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) setState(Failed, tr("%1 could not be started (%2).").arg(m_name, m_command));
    });
    connect(&m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        m_pending.clear();
        if (m_state != Failed) setState(m_shuttingDown ? Stopped : Failed, m_shuttingDown ? QString() : tr("%1 exited (code %2).").arg(m_name).arg(code));
    });
}

LspClient::~LspClient()
{
    disconnect(&m_proc, nullptr, this, nullptr);
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.kill();
        m_proc.waitForFinished(500);
    }
}

QString LspClient::toUri(const QString& path) { return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded); }
QString LspClient::fromUri(const QString& uri) { return QUrl(uri).toLocalFile(); }

void LspClient::setState(State s, const QString& error)
{
    m_state = s;
    m_error = error;
    emit stateChanged();
}

void LspClient::start()
{
    if (m_state == Starting || m_state == Ready) return;
    setState(Starting);
    m_shuttingDown = false;
    m_proc.setWorkingDirectory(m_root);
    m_proc.start(m_command, m_args);
    sendInitialize();
}

void LspClient::sendInitialize()
{
    const QJsonObject caps{
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("synchronization"), QJsonObject{{QStringLiteral("didSave"), true}}},
            {QStringLiteral("completion"), QJsonObject{{QStringLiteral("completionItem"), QJsonObject{
                {QStringLiteral("snippetSupport"), true},
                {QStringLiteral("documentationFormat"), QJsonArray{QStringLiteral("plaintext")}}}}}},
            {QStringLiteral("hover"), QJsonObject{{QStringLiteral("contentFormat"), QJsonArray{QStringLiteral("plaintext"), QStringLiteral("markdown")}}}},
            {QStringLiteral("definition"), QJsonObject{{QStringLiteral("linkSupport"), true}}},
            {QStringLiteral("publishDiagnostics"), QJsonObject{}}}},
        {QStringLiteral("workspace"), QJsonObject{{QStringLiteral("workspaceFolders"), true}}}};
    const QString rootUri = toUri(m_root);
    QJsonObject params{
        {QStringLiteral("processId"), int(QCoreApplication::applicationPid())},
        {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("CrucibleCode")},
                                                   {QStringLiteral("version"), QCoreApplication::applicationVersion()}}},
        {QStringLiteral("rootUri"), rootUri},
        {QStringLiteral("rootPath"), m_root},
        {QStringLiteral("capabilities"), caps},
        {QStringLiteral("workspaceFolders"), QJsonArray{QJsonObject{{QStringLiteral("uri"), rootUri},
                                                                    {QStringLiteral("name"), QDir(m_root).dirName()}}}}};
    request(QStringLiteral("initialize"), params, [this](const QJsonValue&, const QJsonObject& err) {
        if (!err.isEmpty()) {
            setState(Failed, tr("%1 refused to initialise: %2").arg(m_name, err.value(QStringLiteral("message")).toString()));
            return;
        }
        notify(QStringLiteral("initialized"), QJsonObject{});
        setState(Ready);
    });
}

void LspClient::shutdown()
{
    if (m_proc.state() == QProcess::NotRunning) { setState(Stopped); return; }
    m_shuttingDown = true;
    if (m_state == Ready) {
        request(QStringLiteral("shutdown"), QJsonObject{}, [this](const QJsonValue&, const QJsonObject&) {
            notify(QStringLiteral("exit"), QJsonObject{});
        });
    }
    QTimer::singleShot(1500, &m_proc, [p = &m_proc] { if (p->state() != QProcess::NotRunning) p->kill(); });
}

// ---- framing --------------------------------------------------------------------------------------

void LspClient::send(const QJsonObject& msg)
{
    if (m_proc.state() == QProcess::NotRunning && m_state != Starting) return;
    const QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    m_proc.write("Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
}

int LspClient::request(const QString& method, const QJsonObject& params, Callback cb)
{
    const int id = m_nextId++;
    if (cb) m_pending.insert(id, std::move(cb));
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
          {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
    return id;
}

void LspClient::notify(const QString& method, const QJsonObject& params)
{
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
}

void LspClient::onData()
{
    m_buf += m_proc.readAllStandardOutput();
    for (;;) {
        const int headerEnd = int(m_buf.indexOf("\r\n\r\n"));
        if (headerEnd < 0) return;
        int length = -1;
        for (const QByteArray& line : m_buf.left(headerEnd).split('\n'))
            if (line.trimmed().toLower().startsWith("content-length:")) length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
        if (length < 0) { m_buf.remove(0, headerEnd + 4); continue; }
        if (m_buf.size() < headerEnd + 4 + length) return;           // wait for the rest of the body
        const QByteArray body = m_buf.mid(headerEnd + 4, length);
        m_buf.remove(0, headerEnd + 4 + length);
        const QJsonDocument d = QJsonDocument::fromJson(body);
        if (d.isObject()) handle(d.object());
    }
}

void LspClient::handle(const QJsonObject& msg)
{
    const bool hasId = msg.contains(QStringLiteral("id"));
    const QString method = msg.value(QStringLiteral("method")).toString();

    if (!method.isEmpty() && hasId) {                                 // request from the server: answer something sensible
        QJsonValue result;
        if (method == QLatin1String("workspace/configuration")) {
            QJsonArray a;
            for (int i = 0; i < msg.value(QStringLiteral("params")).toObject().value(QStringLiteral("items")).toArray().size(); ++i) a.append(QJsonValue());
            result = a;
        }
        send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), msg.value(QStringLiteral("id"))}, {QStringLiteral("result"), result}});
        return;
    }
    if (!method.isEmpty()) {                                          // notification
        const QJsonObject p = msg.value(QStringLiteral("params")).toObject();
        if (method == QLatin1String("textDocument/publishDiagnostics")) {
            QList<LspDiagnostic> list;
            for (const QJsonValue& v : p.value(QStringLiteral("diagnostics")).toArray()) {
                const QJsonObject o = v.toObject(), r = o.value(QStringLiteral("range")).toObject();
                LspDiagnostic dg;
                dg.line = r.value(QStringLiteral("start")).toObject().value(QStringLiteral("line")).toInt();
                dg.startChar = r.value(QStringLiteral("start")).toObject().value(QStringLiteral("character")).toInt();
                dg.endLine = r.value(QStringLiteral("end")).toObject().value(QStringLiteral("line")).toInt();
                dg.endChar = r.value(QStringLiteral("end")).toObject().value(QStringLiteral("character")).toInt();
                dg.severity = o.value(QStringLiteral("severity")).toInt(1);
                dg.message = o.value(QStringLiteral("message")).toString();
                dg.source = o.value(QStringLiteral("source")).toString();
                dg.code = o.value(QStringLiteral("code")).isString() ? o.value(QStringLiteral("code")).toString()
                                                                       : QString::number(o.value(QStringLiteral("code")).toInt());
                list.append(dg);
            }
            emit diagnosticsReceived(p.value(QStringLiteral("uri")).toString(), list);
        } else if (method == QLatin1String("window/showMessage")) {
            emit logMessage(QStringLiteral("[%1] %2").arg(m_name, p.value(QStringLiteral("message")).toString()));
        }
        return;
    }
    if (hasId) {                                                       // response
        const int id = msg.value(QStringLiteral("id")).toInt();
        const Callback cb = m_pending.take(id);
        if (cb) cb(msg.value(QStringLiteral("result")), msg.value(QStringLiteral("error")).toObject());
    }
}

// ---- documents -------------------------------------------------------------------------------------

void LspClient::didOpen(const QString& uri, const QString& languageId, int version, const QString& text)
{
    notify(QStringLiteral("textDocument/didOpen"), {{QStringLiteral("textDocument"), QJsonObject{
        {QStringLiteral("uri"), uri}, {QStringLiteral("languageId"), languageId},
        {QStringLiteral("version"), version}, {QStringLiteral("text"), text}}}});
}

void LspClient::didChange(const QString& uri, int version, const QString& text)
{
    notify(QStringLiteral("textDocument/didChange"), {
        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}, {QStringLiteral("version"), version}}},
        {QStringLiteral("contentChanges"), QJsonArray{QJsonObject{{QStringLiteral("text"), text}}}}});   // full-document change
}

void LspClient::didSave(const QString& uri)
{
    notify(QStringLiteral("textDocument/didSave"), {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}}}});
}

void LspClient::didClose(const QString& uri)
{
    notify(QStringLiteral("textDocument/didClose"), {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}}}});
}

// ---- queries -----------------------------------------------------------------------------------------

static QJsonObject positionParams(const QString& uri, int line, int ch)
{
    return {{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}}},
            {QStringLiteral("position"), QJsonObject{{QStringLiteral("line"), line}, {QStringLiteral("character"), ch}}}};
}

void LspClient::completion(const QString& uri, int line, int ch, std::function<void(const QList<LspCompletionItem>&)> cb)
{
    request(QStringLiteral("textDocument/completion"), positionParams(uri, line, ch), [cb](const QJsonValue& res, const QJsonObject&) {
        QJsonArray items;
        if (res.isArray()) items = res.toArray();
        else if (res.isObject()) items = res.toObject().value(QStringLiteral("items")).toArray();
        QList<LspCompletionItem> out;
        for (const QJsonValue& v : items) {
            const QJsonObject o = v.toObject();
            LspCompletionItem it;
            it.label = o.value(QStringLiteral("label")).toString().trimmed();
            if (it.label.isEmpty()) continue;
            it.kind = o.value(QStringLiteral("kind")).toInt();
            it.detail = o.value(QStringLiteral("detail")).toString();
            it.sortText = o.value(QStringLiteral("sortText")).toString(it.label);
            const QJsonObject te = o.value(QStringLiteral("textEdit")).toObject();
            QString ins = te.value(QStringLiteral("newText")).toString();
            if (ins.isEmpty()) ins = o.value(QStringLiteral("insertText")).toString();
            if (ins.isEmpty()) ins = it.label;
            it.insertText = ins;
            it.snippet = o.value(QStringLiteral("insertTextFormat")).toInt() == 2;
            out.append(it);
        }
        std::stable_sort(out.begin(), out.end(), [](const LspCompletionItem& a, const LspCompletionItem& b) { return a.sortText < b.sortText; });
        if (out.size() > 400) out.resize(400);
        cb(out);
    });
}

void LspClient::definition(const QString& uri, int line, int ch, std::function<void(const QString&, int, int)> cb)
{
    request(QStringLiteral("textDocument/definition"), positionParams(uri, line, ch), [cb](const QJsonValue& res, const QJsonObject&) {
        QJsonObject loc;
        if (res.isArray() && !res.toArray().isEmpty()) loc = res.toArray().first().toObject();
        else if (res.isObject()) loc = res.toObject();
        QString target = loc.contains(QStringLiteral("targetUri")) ? loc.value(QStringLiteral("targetUri")).toString()
                                                                    : loc.value(QStringLiteral("uri")).toString();
        const QJsonObject range = loc.contains(QStringLiteral("targetSelectionRange")) ? loc.value(QStringLiteral("targetSelectionRange")).toObject()
                                                                                        : loc.value(QStringLiteral("range")).toObject();
        const QJsonObject start = range.value(QStringLiteral("start")).toObject();
        if (target.isEmpty()) { cb(QString(), 0, 0); return; }
        cb(fromUri(target), start.value(QStringLiteral("line")).toInt() + 1, start.value(QStringLiteral("character")).toInt() + 1);
    });
}

static QString markupToText(const QJsonValue& v)
{
    if (v.isString()) return v.toString();
    if (v.isObject()) return v.toObject().value(QStringLiteral("value")).toString();
    if (v.isArray()) {
        QStringList parts;
        for (const QJsonValue& e : v.toArray()) parts << markupToText(e);
        return parts.join(QStringLiteral("\n\n"));
    }
    return {};
}

void LspClient::hover(const QString& uri, int line, int ch, std::function<void(const QString&)> cb)
{
    request(QStringLiteral("textDocument/hover"), positionParams(uri, line, ch), [cb](const QJsonValue& res, const QJsonObject&) {
        cb(res.isObject() ? markupToText(res.toObject().value(QStringLiteral("contents"))).trimmed() : QString());
    });
}
