#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QTimer>
#include <functional>

struct GitChange {
    QString path;        // relative to the repository root
    QString origPath;    // for renames
    QChar index = ' ';   // staged status   (M A D R C), '?' for untracked
    QChar work = ' ';    // working tree status
    bool untracked() const { return index == QLatin1Char('?'); }
    bool staged() const { return index != QLatin1Char(' ') && index != QLatin1Char('?'); }
    bool unstaged() const { return work != QLatin1Char(' ') || untracked(); }
};

struct GitCommit {
    QString hash, subject, author, when;
};

struct GitStatus {
    bool isRepo = false;
    bool gitMissing = false;
    bool noCommits = false;
    bool detached = false;
    QString branch;
    QString upstream;
    int ahead = 0, behind = 0;
    QList<GitChange> changes;
};

// Thin wrapper around the `git` command line: every call runs asynchronously, one at a time.
class GitManager : public QObject {
    Q_OBJECT
public:
    // code: process exit code, -1 crashed, -2 git could not be started
    using Callback = std::function<void(int code, const QString& out, const QString& err)>;

    explicit GitManager(QObject* parent = nullptr);

    void setRoot(const QString& folder);          // the opened project folder
    QString root() const { return m_root; }
    QString topLevel() const { return m_top; }    // repository root (may be above the project folder)
    QString absolutePath(const QString& rel) const;
    const GitStatus& status() const { return m_status; }
    QStringList branches() const { return m_branches; }
    QList<GitCommit> commits() const { return m_commits; }
    bool isBusy() const { return m_busy; }

    void run(const QStringList& args, Callback cb = {}, bool refreshAfter = true);
    void requestRefresh(int delayMs = 250);

    void initRepo();
    void stage(const QStringList& paths);
    void stageAll();
    void unstage(const QStringList& paths);
    void unstageAll();
    void discard(const GitChange& change);
    void commit(const QString& message);
    void pull();
    void push();
    void fetch();
    void checkout(const QString& branch);
    void createBranch(const QString& name);

    void diff(const GitChange& change, bool staged, std::function<void(const QString&)> cb);
    void showCommit(const QString& hash, std::function<void(const QString&)> cb);
    void lineMarks(const QString& absoluteFile, std::function<void(const QHash<int, int>&)> cb);

signals:
    void statusChanged();
    void busyChanged(bool busy);
    void output(const QString& text);
    void failed(const QString& title, const QString& message);

private:
    struct Job {
        QStringList args;
        Callback cb;
        bool refreshAfter = true;
    };

    void pump();
    void refresh();
    void parseStatus(const QString& text);
    void op(const QString& title, const QStringList& args);
    static QString hintFor(const QString& err);

    QString m_root, m_top;
    GitStatus m_status;
    QStringList m_branches;
    QList<GitCommit> m_commits;
    QQueue<Job> m_queue;
    QTimer m_refreshTimer;
    bool m_busy = false;
};
