#include "GitManager.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

GitManager::GitManager(QObject* parent) : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &GitManager::refresh);
}

void GitManager::setRoot(const QString& folder)
{
    m_root = folder;
    m_top.clear();
    m_status = GitStatus();
    m_branches.clear();
    m_commits.clear();
    m_queue.clear();
    emit statusChanged();
    if (!m_root.isEmpty()) requestRefresh(0);
}

QString GitManager::absolutePath(const QString& rel) const
{
    return QDir::cleanPath((m_top.isEmpty() ? m_root : m_top) + QLatin1Char('/') + rel);
}

// ---- process queue -----------------------------------------------------------------------------

void GitManager::run(const QStringList& args, Callback cb, bool refreshAfter)
{
    m_queue.enqueue({args, std::move(cb), refreshAfter});
    pump();
}

void GitManager::pump()
{
    if (m_busy || m_queue.isEmpty()) return;
    const Job job = m_queue.dequeue();
    m_busy = true;
    emit busyChanged(true);

    auto* p = new QProcess(this);
    p->setWorkingDirectory(m_top.isEmpty() ? m_root : m_top);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));   // never wait for a password on a hidden terminal
    env.insert(QStringLiteral("LC_MESSAGES"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    p->setProcessEnvironment(env);

    const auto finish = [this, p, job](int code, const QString& out, const QString& err) {
        p->deleteLater();
        m_busy = false;
        if (job.cb) job.cb(code, out, err);
        if (job.refreshAfter && code == 0) requestRefresh(100);
        emit busyChanged(false);
        pump();
    };
    connect(p, &QProcess::finished, this, [p, finish](int code, QProcess::ExitStatus st) {
        finish(st == QProcess::NormalExit ? code : -1,
               QString::fromUtf8(p->readAllStandardOutput()), QString::fromUtf8(p->readAllStandardError()));
    });
    connect(p, &QProcess::errorOccurred, this, [this, finish](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_status = GitStatus();
        m_status.gitMissing = true;
        emit statusChanged();
        finish(-2, QString(), tr("git could not be started"));
    });
    p->start(QStringLiteral("git"), job.args);
}

void GitManager::requestRefresh(int delayMs)
{
    if (m_root.isEmpty()) return;
    m_refreshTimer.start(delayMs);
}

// ---- status ------------------------------------------------------------------------------------------

void GitManager::refresh()
{
    if (m_root.isEmpty()) return;
    run({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, [this](int code, const QString& out, const QString&) {
        if (code == -2) return;                          // git missing: statusChanged already emitted
        if (code != 0) {                                  // not a repository
            m_top.clear();
            m_status = GitStatus();
            m_branches.clear();
            m_commits.clear();
            emit statusChanged();
            return;
        }
        m_top = QDir::cleanPath(out.trimmed());
        run({QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-b"), QStringLiteral("-z"),
             QStringLiteral("--untracked-files=all")},
            [this](int c, const QString& text, const QString&) {
                if (c != 0) return;
                parseStatus(text);
                run({QStringLiteral("branch"), QStringLiteral("--format=%(refname:short)")},
                    [this](int, const QString& b, const QString&) {
                        m_branches = b.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                        m_branches.removeAll(QString());
                        run({QStringLiteral("log"), QStringLiteral("-30"), QStringLiteral("--no-color"),
                             QStringLiteral("--pretty=format:%h%x1f%s%x1f%an%x1f%ar")},
                            [this](int, const QString& log, const QString&) {
                                m_commits.clear();
                                for (const QString& line : log.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                                    const QStringList f = line.split(QChar(0x1F));
                                    if (f.size() >= 4) m_commits.append(GitCommit{f[0], f[1], f[2], f[3]});
                                }
                                emit statusChanged();
                            }, false);
                    }, false);
            }, false);
    }, false);
}

void GitManager::parseStatus(const QString& text)
{
    GitStatus st;
    st.isRepo = true;
    const QStringList parts = text.split(QChar(0), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString& e = parts.at(i);
        if (e.startsWith(QLatin1String("## "))) {
            static const QRegularExpression re(QStringLiteral("^## (?:No commits yet on |Initial commit on )?(.+?)(?:\\.\\.\\.(\\S+))?(?: \\[(.+)\\])?$"));
            const auto m = re.match(e);
            if (m.hasMatch()) {
                st.branch = m.captured(1);
                st.upstream = m.captured(2);
                const QString track = m.captured(3);
                const auto a = QRegularExpression(QStringLiteral("ahead (\\d+)")).match(track);
                const auto b = QRegularExpression(QStringLiteral("behind (\\d+)")).match(track);
                if (a.hasMatch()) st.ahead = a.captured(1).toInt();
                if (b.hasMatch()) st.behind = b.captured(1).toInt();
            }
            st.noCommits = e.contains(QLatin1String("No commits yet")) || e.contains(QLatin1String("Initial commit"));
            st.detached = st.branch.startsWith(QLatin1String("HEAD (no branch)")) || st.branch.startsWith(QLatin1String("HEAD"));
            continue;
        }
        if (e.size() < 4) continue;
        GitChange c;
        c.index = e.at(0);
        c.work = e.at(1);
        c.path = e.mid(3);
        if ((c.index == QLatin1Char('R') || c.index == QLatin1Char('C')) && i + 1 < parts.size())
            c.origPath = parts.at(++i);
        if (c.index == QLatin1Char('!')) continue;
        st.changes.append(c);
    }
    m_status = st;
}

// ---- operations ------------------------------------------------------------------------------------------

QString GitManager::hintFor(const QString& err)
{
    const QString e = err.toLower();
    if (e.contains(QLatin1String("authentication failed")) || e.contains(QLatin1String("could not read username"))
        || e.contains(QLatin1String("terminal prompts disabled")) || e.contains(QLatin1String("permission denied (publickey")))
        return QObject::tr("\n\nGit needs credentials. Sign in once with Git Credential Manager / an SSH key, or run the command "
                           "once in the integrated terminal (Terminal > New Terminal).");
    if (e.contains(QLatin1String("please tell me who you are")) || e.contains(QLatin1String("empty ident")))
        return QObject::tr("\n\nSet your identity first:\n  git config --global user.name \"Your Name\"\n  git config --global user.email \"you@example.com\"");
    return QString();
}

void GitManager::op(const QString& title, const QStringList& args)
{
    emit output(QStringLiteral("> git ") + args.join(QLatin1Char(' ')));
    run(args, [this, title](int code, const QString& out, const QString& err) {
        if (!out.trimmed().isEmpty()) emit output(out.trimmed());
        if (!err.trimmed().isEmpty()) emit output(err.trimmed());
        if (code != 0) emit failed(title, (err.trimmed().isEmpty() ? out.trimmed() : err.trimmed()) + hintFor(err));
    });
}

void GitManager::initRepo() { op(tr("Initialize Repository"), {QStringLiteral("init")}); }

void GitManager::stage(const QStringList& paths) { op(tr("Stage"), QStringList{QStringLiteral("add"), QStringLiteral("--")} + paths); }
void GitManager::stageAll() { op(tr("Stage All"), {QStringLiteral("add"), QStringLiteral("-A")}); }

void GitManager::unstage(const QStringList& paths)
{
    if (m_status.noCommits) op(tr("Unstage"), QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q"), QStringLiteral("--")} + paths);
    else op(tr("Unstage"), QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD"), QStringLiteral("--")} + paths);
}

void GitManager::unstageAll()
{
    if (m_status.noCommits) op(tr("Unstage All"), {QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q"), QStringLiteral(".")});
    else op(tr("Unstage All"), {QStringLiteral("reset"), QStringLiteral("-q")});
}

void GitManager::discard(const GitChange& c)
{
    if (c.untracked()) op(tr("Discard"), {QStringLiteral("clean"), QStringLiteral("-f"), QStringLiteral("--"), c.path});
    else op(tr("Discard"), {QStringLiteral("checkout"), QStringLiteral("--"), c.path});
}

void GitManager::commit(const QString& message) { op(tr("Commit"), {QStringLiteral("commit"), QStringLiteral("-m"), message}); }

void GitManager::fetch() { op(tr("Fetch"), {QStringLiteral("fetch"), QStringLiteral("--all"), QStringLiteral("--prune")}); }
void GitManager::pull() { op(tr("Pull"), {QStringLiteral("pull")}); }

void GitManager::push()
{
    if (!m_status.upstream.isEmpty()) { op(tr("Push"), {QStringLiteral("push")}); return; }
    // no upstream yet: publish the branch to the first remote
    run({QStringLiteral("remote")}, [this](int code, const QString& out, const QString&) {
        const QStringList remotes = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (code != 0 || remotes.isEmpty()) {
            emit failed(tr("Push"), tr("This repository has no remote yet. Add one in the terminal, for example:\n"
                                       "  git remote add origin https://github.com/you/project.git"));
            return;
        }
        op(tr("Push"), {QStringLiteral("push"), QStringLiteral("-u"), remotes.first().trimmed(), QStringLiteral("HEAD")});
    }, false);
}

void GitManager::checkout(const QString& branch) { op(tr("Checkout"), {QStringLiteral("checkout"), branch}); }
void GitManager::createBranch(const QString& name) { op(tr("Create Branch"), {QStringLiteral("checkout"), QStringLiteral("-b"), name}); }

// ---- diffs ---------------------------------------------------------------------------------------------------

void GitManager::diff(const GitChange& c, bool staged, std::function<void(const QString&)> cb)
{
    QStringList args;
    if (c.untracked())
        args = {QStringLiteral("diff"), QStringLiteral("--no-color"), QStringLiteral("--no-index"), QStringLiteral("--"),
                QStringLiteral("/dev/null"), c.path};
    else if (staged)
        args = {QStringLiteral("diff"), QStringLiteral("--no-color"), QStringLiteral("--cached"), QStringLiteral("--"), c.path};
    else
        args = {QStringLiteral("diff"), QStringLiteral("--no-color"), QStringLiteral("--"), c.path};
    run(args, [cb](int code, const QString& out, const QString& err) {
        // `diff --no-index` exits with 1 when files differ
        cb(out.isEmpty() ? (code > 1 ? err : QObject::tr("(no differences)")) : out);
    }, false);
}

void GitManager::showCommit(const QString& hash, std::function<void(const QString&)> cb)
{
    run({QStringLiteral("show"), QStringLiteral("--no-color"), QStringLiteral("--stat"), QStringLiteral("--patch"), hash},
        [cb](int, const QString& out, const QString& err) { cb(out.isEmpty() ? err : out); }, false);
}

// Gutter markers: parse `git diff -U0` hunks into line -> kind (1 added, 2 modified, 3 deleted-above).
void GitManager::lineMarks(const QString& absoluteFile, std::function<void(const QHash<int, int>&)> cb)
{
    if (m_top.isEmpty()) { cb({}); return; }
    const QString rel = QDir(m_top).relativeFilePath(absoluteFile);
    if (rel.startsWith(QLatin1String(".."))) { cb({}); return; }
    QStringList args = {QStringLiteral("diff"), QStringLiteral("--no-color"), QStringLiteral("-U0")};
    if (!m_status.noCommits) args << QStringLiteral("HEAD");
    args << QStringLiteral("--") << rel;
    run(args, [cb](int code, const QString& out, const QString&) {
        QHash<int, int> marks;
        if (code != 0) { cb(marks); return; }
        static const QRegularExpression hunk(QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));
        for (const QString& line : out.split(QLatin1Char('\n'))) {
            const auto m = hunk.match(line);
            if (!m.hasMatch()) continue;
            const int oldCount = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
            const int start = m.captured(3).toInt();
            const int newCount = m.captured(4).isEmpty() ? 1 : m.captured(4).toInt();
            if (newCount == 0) {
                marks.insert(qMax(1, start + 1), 3);
            } else {
                for (int l = start; l < start + newCount; ++l) marks.insert(l, oldCount == 0 ? 1 : 2);
            }
        }
        cb(marks);
    }, false);
}
