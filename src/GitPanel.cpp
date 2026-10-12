#include "GitPanel.h"
#include "GitManager.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

enum Kind { KindGroup = 0, KindStaged = 1, KindUnstaged = 2, KindCommit = 3 };
constexpr int RoleKind = Qt::UserRole;
constexpr int RolePath = Qt::UserRole + 1;
constexpr int RoleLetter = Qt::UserRole + 2;
constexpr int RoleGroupId = Qt::UserRole + 3;   // 0 staged, 1 changes, 2 commits

class DiffHighlighter : public QSyntaxHighlighter {
public:
    explicit DiffHighlighter(QTextDocument* d) : QSyntaxHighlighter(d) {}

protected:
    void highlightBlock(const QString& text) override
    {
        const ThemeColors& c = ThemeManager::instance().colors();
        QTextCharFormat f;
        if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---")) || text.startsWith(QLatin1String("diff "))
            || text.startsWith(QLatin1String("index "))) {
            f.setForeground(c.textDim);
            f.setFontWeight(QFont::Bold);
        } else if (text.startsWith(QLatin1Char('+'))) {
            QColor bg = c.gitAdded;
            bg.setAlpha(70);
            f.setBackground(bg);
        } else if (text.startsWith(QLatin1Char('-'))) {
            QColor bg = c.gitDeleted;
            bg.setAlpha(70);
            f.setBackground(bg);
        } else if (text.startsWith(QLatin1String("@@"))) {
            f.setForeground(c.keyword);
        } else if (text.startsWith(QLatin1String("commit "))) {
            f.setForeground(c.function);
            f.setFontWeight(QFont::Bold);
        } else {
            return;
        }
        setFormat(0, int(text.size()), f);
    }
};

QColor letterColor(QChar l)
{
    const ThemeColors& c = ThemeManager::instance().colors();
    if (l == QLatin1Char('A') || l == QLatin1Char('U') || l == QLatin1Char('?')) return c.gitAdded;
    if (l == QLatin1Char('D')) return c.gitDeleted;
    return c.gitModified;
}

} // namespace

// ---------------------------------------------------------------------------------------------
//  DiffViewer
// ---------------------------------------------------------------------------------------------

DiffViewer::DiffViewer(QWidget* parent) : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("codeEditor"));
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameStyle(QFrame::NoFrame);
    new DiffHighlighter(document());
}

void DiffViewer::setDiff(const QString& text)
{
    setPlainText(text);
    moveCursor(QTextCursor::Start);
}

// ---------------------------------------------------------------------------------------------
//  GitPanel
// ---------------------------------------------------------------------------------------------

GitPanel::GitPanel(GitManager* git, QWidget* parent) : QWidget(parent), m_git(git)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_pages = new QStackedWidget;
    root->addWidget(m_pages);

    // ---- page 0: not a repository / git missing
    auto* info = new QWidget;
    auto* il = new QVBoxLayout(info);
    il->setContentsMargins(10, 12, 10, 10);
    m_message = new QLabel;
    m_message->setWordWrap(true);
    m_initBtn = new QPushButton(tr("Initialize Repository"));
    il->addWidget(m_message);
    il->addWidget(m_initBtn);
    il->addStretch(1);
    m_pages->addWidget(info);

    // ---- page 1: repository
    auto* repo = new QWidget;
    auto* rl = new QVBoxLayout(repo);
    rl->setContentsMargins(4, 4, 4, 4);
    rl->setSpacing(4);

    auto* top = new QHBoxLayout;
    m_branch = new QComboBox;
    m_branch->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_branch->setMinimumContentsLength(10);
    m_branch->setToolTip(tr("Current branch (select to switch)"));
    auto* refresh = new QPushButton(tr("Refresh"));
    top->addWidget(m_branch, 1);
    top->addWidget(refresh);
    rl->addLayout(top);

    auto* sync = new QHBoxLayout;
    auto* fetchBtn = new QPushButton(tr("Fetch"));
    m_pullBtn = new QPushButton(tr("Pull"));
    m_pushBtn = new QPushButton(tr("Push"));
    m_sync = new QLabel;
    sync->addWidget(fetchBtn);
    sync->addWidget(m_pullBtn);
    sync->addWidget(m_pushBtn);
    sync->addWidget(m_sync, 1);
    rl->addLayout(sync);

    m_commitMsg = new QPlainTextEdit;
    m_commitMsg->setPlaceholderText(tr("Message (Ctrl+Enter to commit)"));
    m_commitMsg->setFixedHeight(64);
    m_commitMsg->installEventFilter(this);
    rl->addWidget(m_commitMsg);

    auto* actions = new QHBoxLayout;
    m_commitBtn = new QPushButton(tr("Commit"));
    auto* stageAll = new QPushButton(tr("Stage All"));
    auto* unstageAll = new QPushButton(tr("Unstage All"));
    actions->addWidget(m_commitBtn, 1);
    actions->addWidget(stageAll);
    actions->addWidget(unstageAll);
    rl->addLayout(actions);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setIndentation(12);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    rl->addWidget(m_tree, 1);
    m_pages->addWidget(repo);

    // ---- wiring
    connect(m_git, &GitManager::statusChanged, this, &GitPanel::rebuild);
    connect(m_initBtn, &QPushButton::clicked, m_git, &GitManager::initRepo);
    connect(refresh, &QPushButton::clicked, this, [this] { m_git->requestRefresh(0); });
    connect(fetchBtn, &QPushButton::clicked, m_git, &GitManager::fetch);
    connect(m_pullBtn, &QPushButton::clicked, m_git, &GitManager::pull);
    connect(m_pushBtn, &QPushButton::clicked, m_git, &GitManager::push);
    connect(stageAll, &QPushButton::clicked, m_git, &GitManager::stageAll);
    connect(unstageAll, &QPushButton::clicked, m_git, &GitManager::unstageAll);
    connect(m_commitBtn, &QPushButton::clicked, this, &GitPanel::commitClicked);
    connect(m_branch, &QComboBox::activated, this, &GitPanel::branchActivated);
    connect(m_tree, &QTreeWidget::itemActivated, this, &GitPanel::itemActivated);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &GitPanel::contextMenu);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
        if (!m_rebuilding && it->data(0, RoleGroupId).toInt() == 2) m_commitsExpanded = true;
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
        if (!m_rebuilding && it->data(0, RoleGroupId).toInt() == 2) m_commitsExpanded = false;
    });
    connect(m_git, &GitManager::failed, this, [this](const QString& title, const QString& msg) {
        QMessageBox::warning(this, title, msg);
    });

    rebuild();
}

bool GitPanel::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_commitMsg && ev->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(ev);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && (k->modifiers() & Qt::ControlModifier)) {
            commitClicked();
            return true;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

void GitPanel::rebuild()
{
    const GitStatus& st = m_git->status();
    if (!st.isRepo) {
        m_pages->setCurrentIndex(0);
        if (m_git->root().isEmpty()) {
            m_message->setText(tr("Open a folder to use Source Control."));
            m_initBtn->hide();
        } else if (st.gitMissing) {
            m_message->setText(tr("<b>Git was not found.</b><br>Install it to use Source Control:<br>"
                                  "Windows: <tt>winget install Git.Git</tt><br>macOS: <tt>brew install git</tt><br>"
                                  "Ubuntu/Debian: <tt>sudo apt install git</tt><br>Fedora: <tt>sudo dnf install git</tt><br><br>"
                                  "Then restart CrucibleCode."));
            m_initBtn->hide();
        } else if (m_git->topLevel().isEmpty() && !st.isRepo) {
            m_message->setText(tr("The opened folder is not a Git repository."));
            m_initBtn->show();
        }
        return;
    }
    m_pages->setCurrentIndex(1);
    m_rebuilding = true;

    // branch selector
    {
        const QSignalBlocker block(m_branch);
        m_branch->clear();
        QStringList names = m_git->branches();
        const QString current = st.detached ? tr("(detached HEAD)") : st.branch;
        if (!names.contains(current)) names.prepend(current);
        m_branch->addItems(names);
        m_branch->setCurrentText(current);
        m_branch->addItem(tr("+ Create new branch..."));
    }
    QStringList sync;
    if (st.ahead) sync << tr("%1 to push").arg(st.ahead);
    if (st.behind) sync << tr("%1 to pull").arg(st.behind);
    if (st.upstream.isEmpty() && !st.noCommits) sync << tr("not published");
    m_sync->setText(sync.join(QStringLiteral(", ")));
    m_pushBtn->setText(st.upstream.isEmpty() ? tr("Publish") : tr("Push"));

    // change groups
    m_tree->clear();
    QList<GitChange> staged, changes;
    for (const GitChange& c : st.changes) {
        if (c.staged()) staged << c;
        if (c.unstaged()) changes << c;
    }
    const auto addGroup = [&](const QString& title, int groupId) {
        auto* g = new QTreeWidgetItem(m_tree);
        g->setText(0, title);
        g->setData(0, RoleKind, KindGroup);
        g->setData(0, RoleGroupId, groupId);
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFirstColumnSpanned(true);
        return g;
    };
    const auto addFile = [&](QTreeWidgetItem* group, const GitChange& c, bool isStaged) {
        auto* it = new QTreeWidgetItem(group);
        const QFileInfo fi(c.path);
        const QString dir = fi.path() == QLatin1String(".") ? QString() : fi.path();
        it->setText(0, fi.fileName() + (dir.isEmpty() ? QString() : QStringLiteral("   ") + dir));
        it->setToolTip(0, c.path);
        QChar letter = isStaged ? c.index : (c.untracked() ? QLatin1Char('U') : c.work);
        it->setText(1, QString(letter));
        it->setForeground(1, letterColor(letter));
        it->setData(0, RoleKind, isStaged ? KindStaged : KindUnstaged);
        it->setData(0, RolePath, c.path);
        it->setData(0, RoleLetter, QString(letter));
    };

    if (!staged.isEmpty()) {
        auto* g = addGroup(tr("Staged Changes (%1)").arg(staged.size()), 0);
        for (const GitChange& c : staged) addFile(g, c, true);
        g->setExpanded(true);
    }
    auto* g = addGroup(tr("Changes (%1)").arg(changes.size()), 1);
    for (const GitChange& c : changes) addFile(g, c, false);
    g->setExpanded(true);

    const QList<GitCommit> commits = m_git->commits();
    if (!commits.isEmpty()) {
        auto* cg = addGroup(tr("Recent Commits"), 2);
        for (const GitCommit& c : commits) {
            auto* it = new QTreeWidgetItem(cg);
            it->setText(0, c.subject);
            it->setText(1, c.hash);
            it->setToolTip(0, tr("%1\n%2, %3").arg(c.subject, c.author, c.when));
            it->setData(0, RoleKind, KindCommit);
            it->setData(0, RolePath, c.hash);
        }
        cg->setExpanded(m_commitsExpanded);
    }
    m_commitBtn->setEnabled(!staged.isEmpty() || !changes.isEmpty());
    m_rebuilding = false;
}

void GitPanel::branchActivated(int index)
{
    if (index < 0) return;
    if (index == m_branch->count() - 1) {          // "+ Create new branch..."
        const QString name = QInputDialog::getText(this, tr("Create Branch"), tr("New branch name:")).trimmed();
        if (!name.isEmpty()) m_git->createBranch(name);
        rebuild();
        return;
    }
    const QString name = m_branch->itemText(index);
    if (name != m_git->status().branch) m_git->checkout(name);
}

void GitPanel::commitClicked()
{
    const QString msg = m_commitMsg->toPlainText().trimmed();
    if (msg.isEmpty()) {
        QMessageBox::information(this, tr("Commit"), tr("Type a commit message first."));
        m_commitMsg->setFocus();
        return;
    }
    bool anyStaged = false;
    for (const GitChange& c : m_git->status().changes) anyStaged = anyStaged || c.staged();
    if (!anyStaged) {
        const auto r = QMessageBox::question(this, tr("Commit"), tr("There are no staged changes.\nStage all changes and commit?"));
        if (r != QMessageBox::Yes) return;
        m_git->stageAll();
    }
    m_git->commit(msg);
    m_commitMsg->clear();
}

void GitPanel::itemActivated(QTreeWidgetItem* item)
{
    const int kind = item->data(0, RoleKind).toInt();
    const QString path = item->data(0, RolePath).toString();
    if (kind == KindCommit) {
        m_git->showCommit(path, [this, path](const QString& text) {
            emit showDiffRequested(QStringLiteral("commit:") + path, tr("Commit %1").arg(path), text);
        });
        return;
    }
    if (kind != KindStaged && kind != KindUnstaged) return;

    GitChange change;
    for (const GitChange& c : m_git->status().changes)
        if (c.path == path) { change = c; break; }
    const bool staged = kind == KindStaged;
    if (change.path.isEmpty()) return;
    m_git->diff(change, staged, [this, path, staged](const QString& text) {
        emit showDiffRequested(QStringLiteral("%1:%2").arg(staged ? QStringLiteral("staged") : QStringLiteral("work"), path),
                               tr("%1 (%2)").arg(QFileInfo(path).fileName(), staged ? tr("Staged") : tr("Changes")), text);
    });
}

void GitPanel::contextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;
    const int kind = item->data(0, RoleKind).toInt();
    QMenu menu(this);

    if (kind == KindCommit) {
        QAction* show = menu.addAction(tr("Show Commit"));
        QAction* copy = menu.addAction(tr("Copy Hash"));
        QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == show) itemActivated(item);
        else if (chosen == copy) QApplication::clipboard()->setText(item->data(0, RolePath).toString());
        return;
    }
    if (kind == KindGroup) {
        const int gid = item->data(0, RoleGroupId).toInt();
        if (gid == 2) return;
        QAction* a = menu.addAction(gid == 0 ? tr("Unstage All") : tr("Stage All"));
        if (menu.exec(m_tree->viewport()->mapToGlobal(pos)) == a) {
            if (gid == 0) m_git->unstageAll();
            else m_git->stageAll();
        }
        return;
    }

    // collect the selected files of this kind
    QStringList paths;
    for (QTreeWidgetItem* it : m_tree->selectedItems())
        if (it->data(0, RoleKind).toInt() == kind) paths << it->data(0, RolePath).toString();
    if (paths.isEmpty()) paths << item->data(0, RolePath).toString();

    QAction* openFile = menu.addAction(tr("Open File"));
    QAction* openDiff = menu.addAction(tr("Open Changes"));
    menu.addSeparator();
    QAction* stage = nullptr;
    QAction* unstage = nullptr;
    QAction* discard = nullptr;
    if (kind == KindUnstaged) {
        stage = menu.addAction(tr("Stage Changes"));
        discard = menu.addAction(tr("Discard Changes..."));
    } else {
        unstage = menu.addAction(tr("Unstage Changes"));
    }
    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == openFile) {
        emit openFileRequested(m_git->absolutePath(paths.first()));
    } else if (chosen == openDiff) {
        itemActivated(item);
    } else if (chosen == stage) {
        m_git->stage(paths);
    } else if (chosen == unstage) {
        m_git->unstage(paths);
    } else if (chosen == discard) {
        const auto r = QMessageBox::warning(this, tr("Discard Changes"),
            tr("Discard changes in %n file(s)?\nThis cannot be undone.", nullptr, int(paths.size())),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
        for (const QString& p : paths)
            for (const GitChange& c : m_git->status().changes)
                if (c.path == p) m_git->discard(c);
    }
}
