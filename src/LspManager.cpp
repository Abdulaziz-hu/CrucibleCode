#include "LspManager.h"
#include "CodeEditor.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

LspManager::LspManager(QObject* parent) : QObject(parent) {}
LspManager::~LspManager() { shutdownAll(); }

// ---- configuration -----------------------------------------------------------------------------------

QList<LspServerConfig> LspManager::defaultServers()
{
    const auto mk = [](const char* name, const char* exts, const char* cmd, const char* args) {
        LspServerConfig c;
        c.name = QString::fromLatin1(name);
        c.extensions = QString::fromLatin1(exts).split(QLatin1Char(','), Qt::SkipEmptyParts);
        c.command = QString::fromLatin1(cmd);
        c.args = QString::fromLatin1(args).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        return c;
    };
    return {mk("clangd (C / C++)", "c,cc,cpp,cxx,h,hh,hpp,hxx,inl,ipp,cu", "clangd", "--background-index"),
            mk("OmniSharp-compatible (C#)", "cs", "csharp-ls", ""),
            mk("Python (pylsp)", "py,pyw", "pylsp", ""),
            mk("rust-analyzer", "rs", "rust-analyzer", ""),
            mk("gopls (Go)", "go", "gopls", ""),
            mk("TypeScript / JavaScript", "ts,tsx,js,jsx,mjs,cjs", "typescript-language-server", "--stdio")};
}

QList<LspServerConfig> LspManager::loadServers()
{
    const QByteArray json = QSettings().value(QStringLiteral("lsp/servers")).toString().toUtf8();
    if (json.isEmpty()) return defaultServers();
    QList<LspServerConfig> list;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        LspServerConfig c;
        c.name = o.value(QStringLiteral("name")).toString();
        for (const QJsonValue& e : o.value(QStringLiteral("extensions")).toArray()) c.extensions << e.toString();
        c.command = o.value(QStringLiteral("command")).toString();
        for (const QJsonValue& a : o.value(QStringLiteral("args")).toArray()) c.args << a.toString();
        c.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (!c.name.isEmpty()) list << c;
    }
    return list;
}

void LspManager::saveServers(const QList<LspServerConfig>& list)
{
    QJsonArray a;
    for (const LspServerConfig& c : list)
        a.append(QJsonObject{{QStringLiteral("name"), c.name}, {QStringLiteral("extensions"), QJsonArray::fromStringList(c.extensions)},
                             {QStringLiteral("command"), c.command}, {QStringLiteral("args"), QJsonArray::fromStringList(c.args)},
                             {QStringLiteral("enabled"), c.enabled}});
    QSettings().setValue(QStringLiteral("lsp/servers"), QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}

bool LspManager::globalEnabled() { return QSettings().value(QStringLiteral("lsp/enabled"), true).toBool(); }
void LspManager::setGlobalEnabled(bool on) { QSettings().setValue(QStringLiteral("lsp/enabled"), on); }

bool LspManager::commandAvailable(const QString& command)
{
    if (command.trimmed().isEmpty()) return false;
    if (QFileInfo(command).isAbsolute()) return QFileInfo(command).isExecutable();
    return !QStandardPaths::findExecutable(command).isEmpty();
}

QString LspManager::languageIdFor(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    static const QHash<QString, QString> map = {
        {"c", "c"}, {"h", "cpp"}, {"cc", "cpp"}, {"cpp", "cpp"}, {"cxx", "cpp"}, {"hh", "cpp"}, {"hpp", "cpp"}, {"hxx", "cpp"},
        {"inl", "cpp"}, {"ipp", "cpp"}, {"cu", "cuda-cpp"}, {"cs", "csharp"}, {"py", "python"}, {"pyw", "python"}, {"rs", "rust"},
        {"go", "go"}, {"ts", "typescript"}, {"tsx", "typescriptreact"}, {"js", "javascript"}, {"mjs", "javascript"},
        {"cjs", "javascript"}, {"jsx", "javascriptreact"}};
    return map.value(ext, ext);
}

QString LspManager::findCompileCommandsDir() const
{
    for (const QString& rel : {QString(), QStringLiteral("build"), QStringLiteral("build/debug"), QStringLiteral("build/release"),
                               QStringLiteral("out/build"), QStringLiteral("cmake-build-debug")}) {
        const QString d = rel.isEmpty() ? m_root : m_root + QLatin1Char('/') + rel;
        if (QFileInfo::exists(d + QStringLiteral("/compile_commands.json"))) return d;
    }
    return {};
}

// ---- servers / documents ------------------------------------------------------------------------------

void LspManager::setProjectRoot(const QString& root)
{
    if (root == m_root) return;
    // documents of the old project are closed by the main window before this is called
    shutdownAll();
    m_root = root;
}

LspServerConfig LspManager::configFor(const QString& path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();
    for (const LspServerConfig& c : loadServers())
        if (c.enabled && c.extensions.contains(ext, Qt::CaseInsensitive)) return c;
    return {};
}

LspClient* LspManager::clientFor(const LspServerConfig& cfg)
{
    if (LspClient* c = m_clients.value(cfg.name)) return c;
    if (!commandAvailable(cfg.command)) {
        if (!m_unavailable.contains(cfg.name)) {
            m_unavailable.insert(cfg.name, tr("\"%1\" was not found. Install it or set its path in Settings > Language Servers.").arg(cfg.command));
            emit statusChanged();
        }
        return nullptr;
    }
    QStringList args = cfg.args;
    if (QFileInfo(cfg.command).completeBaseName() == QLatin1String("clangd")) {
        const QString dir = findCompileCommandsDir();
        if (!dir.isEmpty()) args << QStringLiteral("--compile-commands-dir=") + dir;
    }
    const QString root = m_root.isEmpty() ? QDir::homePath() : m_root;
    auto* c = new LspClient(cfg.name, cfg.command, args, root, this);
    connect(c, &LspClient::stateChanged, this, [this, c] { clientStateChanged(c); });
    connect(c, &LspClient::logMessage, this, &LspManager::message);
    connect(c, &LspClient::diagnosticsReceived, this, [this](const QString& uri, const QList<LspDiagnostic>& list) {
        const QString path = LspClient::fromUri(uri);
        if (list.isEmpty()) m_problems.remove(path); else m_problems.insert(path, list);
        for (auto it = m_docs.begin(); it != m_docs.end(); ++it)
            if (it->uri == uri && it->editor) it->editor->setLspDiagnostics(list);
        emit problemsChanged();
        emit statusChanged();
    });
    m_clients.insert(cfg.name, c);
    c->start();
    return c;
}

LspManager::Doc* LspManager::docFor(CodeEditor* e)
{
    const auto it = m_docs.find(e);
    return it == m_docs.end() ? nullptr : &it.value();
}

void LspManager::attach(CodeEditor* e)
{
    if (!e || e->isUntitled() || !globalEnabled() || m_docs.contains(e)) return;
    const LspServerConfig cfg = configFor(e->filePath());
    if (cfg.name.isEmpty()) return;
    LspClient* client = clientFor(cfg);
    if (!client) return;

    Doc d;
    d.editor = e;
    d.path = e->filePath();
    d.uri = LspClient::toUri(d.path);
    d.languageId = languageIdFor(d.path);
    d.client = client;
    d.serverName = cfg.name;
    d.timer = new QTimer(this);
    d.timer->setSingleShot(true);
    d.timer->setInterval(250);
    m_docs.insert(e, d);

    connect(d.timer, &QTimer::timeout, this, [this, e] { if (Doc* doc = docFor(e)) flush(*doc); });
    connect(e->document(), &QTextDocument::contentsChanged, d.timer, qOverload<>(&QTimer::start));
    connect(e, &CodeEditor::lspCompleteRequested, this, [this, e](bool manual) { complete(e, manual); });
    connect(e, &CodeEditor::lspDefinitionRequested, this, [this, e] { gotoDefinition(e); });
    connect(e, &QObject::destroyed, this, [this, e] {
        const auto it = m_docs.find(e);
        if (it == m_docs.end()) return;
        if (it->client && it->opened) it->client->didClose(it->uri);
        m_problems.remove(it->path);
        it->timer->deleteLater();
        m_docs.erase(it);
        emit problemsChanged();
        emit statusChanged();
    });
    if (client->state() == LspClient::Ready) clientStateChanged(client);
    emit statusChanged();
}

void LspManager::detach(CodeEditor* e)
{
    const auto it = m_docs.find(e);
    if (it == m_docs.end()) return;
    if (it->client && it->opened) it->client->didClose(it->uri);
    m_problems.remove(it->path);
    it->timer->deleteLater();
    disconnect(e->document(), nullptr, it->timer, nullptr);
    disconnect(e, nullptr, this, nullptr);
    e->setLspActive(false);
    e->setLspDiagnostics({});
    m_docs.erase(it);
    emit problemsChanged();
    emit statusChanged();
}

void LspManager::notifySaved(CodeEditor* e)
{
    Doc* d = docFor(e);
    if (!d) { attach(e); return; }                              // untitled file that just got a name
    if (d->path != e->filePath()) { detach(e); attach(e); return; }   // Save As
    if (d->timer->isActive()) { d->timer->stop(); flush(*d); }
    if (d->opened) d->client->didSave(d->uri);
}

void LspManager::flush(Doc& d)
{
    if (!d.editor || !d.opened || !d.client) return;
    d.client->didChange(d.uri, ++d.version, d.editor->toPlainText());
}

void LspManager::clientStateChanged(LspClient* c)
{
    for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
        if (it->client != c || !it->editor) continue;
        if (c->state() == LspClient::Ready) {
            if (!it->opened) {
                it->client->didOpen(it->uri, it->languageId, it->version, it->editor->toPlainText());
                it->opened = true;
            }
            it->editor->setLspActive(true);
        } else {
            it->editor->setLspActive(false);
            it->opened = false;
            if (c->state() == LspClient::Failed) it->editor->setLspDiagnostics({});
        }
    }
    if (c->state() == LspClient::Failed) {
        emit message(c->errorText());
        m_clients.remove(c->name() );
        // keep the failed client around for the status text, but allow a later retry through reloadSettings()
        m_unavailable.insert(c->name(), c->errorText());
        c->deleteLater();
    }
    emit statusChanged();
}

void LspManager::reloadSettings()
{
    QList<QPointer<CodeEditor>> eds;
    for (auto it = m_docs.begin(); it != m_docs.end(); ++it) eds << it->editor;
    for (const auto& e : eds) if (e) detach(e);
    shutdownAll();
    m_unavailable.clear();
    for (const auto& e : eds) if (e) attach(e);
    emit statusChanged();
}

void LspManager::shutdownAll()
{
    for (auto it = m_docs.begin(); it != m_docs.end(); ++it) {
        if (it->editor) { it->editor->setLspActive(false); it->editor->setLspDiagnostics({}); }
        it->opened = false;
    }
    for (LspClient* c : std::as_const(m_clients)) {
        c->setParent(nullptr);                       // finishes on its own timers; deleted when the process ends
        QObject::connect(c, &LspClient::stateChanged, c, [c] { if (c->state() != LspClient::Starting && c->state() != LspClient::Ready) c->deleteLater(); });
        c->shutdown();
    }
    m_clients.clear();
    m_problems.clear();
    emit problemsChanged();
    emit statusChanged();
}

// ---- requests ------------------------------------------------------------------------------------------

void LspManager::complete(CodeEditor* e, bool manual)
{
    Doc* d = docFor(e);
    if (!d || !d->opened) return;
    if (d->timer->isActive()) { d->timer->stop(); flush(*d); }
    const QTextCursor c = e->textCursor();
    const int block = c.blockNumber();
    QPointer<CodeEditor> ed(e);
    d->client->completion(d->uri, block, c.positionInBlock(), [this, ed, block, manual](const QList<LspCompletionItem>& items) {
        if (!ed || ed->textCursor().blockNumber() != block) return;
        if (items.isEmpty() && manual) emit message(tr("No completions available here."));
        else ed->showCompletionItems(items);
    });
}

void LspManager::gotoDefinition(CodeEditor* e)
{
    Doc* d = docFor(e);
    if (!d || !d->opened) { emit message(tr("Go to Definition needs a running language server for this file type.")); return; }
    if (d->timer->isActive()) { d->timer->stop(); flush(*d); }
    const QTextCursor c = e->textCursor();
    d->client->definition(d->uri, c.blockNumber(), c.positionInBlock(), [this](const QString& path, int line, int col) {
        if (path.isEmpty()) emit message(tr("No definition found."));
        else emit locationRequested(path, line, col);
    });
}

void LspManager::hover(CodeEditor* e)
{
    Doc* d = docFor(e);
    if (!d || !d->opened) return;
    if (d->timer->isActive()) { d->timer->stop(); flush(*d); }
    const QTextCursor c = e->textCursor();
    QPointer<CodeEditor> ed(e);
    d->client->hover(d->uri, c.blockNumber(), c.positionInBlock(), [this, ed](const QString& text) {
        if (!ed) return;
        if (text.isEmpty()) emit message(tr("No information available here."));
        else ed->showHoverText(text);
    });
}

// ---- status ------------------------------------------------------------------------------------------------

int LspManager::count(int severity) const
{
    int n = 0;
    for (const auto& list : m_problems) for (const LspDiagnostic& d : list) if (d.severity == severity) ++n;
    return n;
}

QString LspManager::statusText() const
{
    if (!globalEnabled() || (m_clients.isEmpty() && m_unavailable.isEmpty() && m_docs.isEmpty())) return {};
    QStringList parts;
    bool ready = false, starting = false;
    for (LspClient* c : m_clients) {
        if (c->state() == LspClient::Ready) ready = true;
        else if (c->state() == LspClient::Starting) starting = true;
    }
    if (ready) parts << tr("LSP");
    else if (starting) parts << tr("LSP...");
    else if (!m_unavailable.isEmpty()) parts << tr("LSP off");
    else return {};
    const int e = count(1), w = count(2);
    if (e || w) parts << tr("%1 E, %2 W").arg(e).arg(w);
    return parts.join(QStringLiteral("  "));
}

QString LspManager::statusTooltip() const
{
    QStringList l;
    for (LspClient* c : m_clients)
        l << QStringLiteral("%1: %2").arg(c->name(), c->state() == LspClient::Ready ? tr("running") : c->state() == LspClient::Starting ? tr("starting") : tr("stopped"));
    for (auto it = m_unavailable.begin(); it != m_unavailable.end(); ++it) l << QStringLiteral("%1: %2").arg(it.key(), it.value());
    return l.join(QLatin1Char('\n'));
}
