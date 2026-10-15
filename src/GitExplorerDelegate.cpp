#include "GitExplorerDelegate.h"
#include "GitManager.h"
#include "ThemeManager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QFileSystemModel>
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

GitExplorerDelegate::GitExplorerDelegate(GitManager* git, QFileSystemModel* model, QObject* parent)
    : QStyledItemDelegate(parent), m_git(git), m_model(model) {}

QColor GitExplorerDelegate::colorFor(QChar l)
{
    const ThemeColors& c = ThemeManager::instance().colors();
    if (l == QLatin1Char('A') || l == QLatin1Char('U')) return c.gitAdded;
    if (l == QLatin1Char('D')) return c.gitDeleted;
    if (l == QLatin1Char('!')) return c.error;
    return c.gitModified;
}

QString GitExplorerDelegate::describe(QChar l)
{
    switch (l.unicode()) {
    case 'M': return QObject::tr("Modified");
    case 'A': return QObject::tr("Added (staged)");
    case 'U': return QObject::tr("Untracked");
    case 'D': return QObject::tr("Deleted");
    case 'R': return QObject::tr("Renamed");
    case '!': return QObject::tr("Merge conflict");
    default: return {};
    }
}

// folders show the most important status of their content: conflict > deleted > modified > new
int GitExplorerDelegate::rank(QChar l)
{
    switch (l.unicode()) {
    case '!': return 4;
    case 'D': return 3;
    case 'M': case 'R': return 2;
    case 'A': case 'U': return 1;
    default: return 0;
    }
}

void GitExplorerDelegate::rebuild()
{
    m_files.clear();
    m_dirs.clear();
    m_untrackedDirs.clear();
    const GitStatus& st = m_git->status();
    if (!st.isRepo) return;
    const QString top = QDir::cleanPath(m_git->topLevel());
    for (const GitChange& c : st.changes) {
        QChar l;
        const bool conflict = c.index == QLatin1Char('U') || c.work == QLatin1Char('U')
            || (c.index == QLatin1Char('A') && c.work == QLatin1Char('A')) || (c.index == QLatin1Char('D') && c.work == QLatin1Char('D'));
        if (conflict) l = QLatin1Char('!');
        else if (c.untracked()) l = QLatin1Char('U');
        else {
            l = c.work != QLatin1Char(' ') ? c.work : c.index;
            if (l == QLatin1Char('T')) l = QLatin1Char('M');
            if (l == QLatin1Char('C')) l = QLatin1Char('A');
        }
        const bool isDir = c.path.endsWith(QLatin1Char('/'));
        QString abs = QDir::cleanPath(top + QLatin1Char('/') + c.path);
        if (isDir) m_untrackedDirs.insert(abs);
        else m_files.insert(abs, l);
        // propagate to every parent folder up to the repository root
        QString dir = isDir ? abs : QFileInfo(abs).absolutePath();
        for (int guard = 0; guard < 64 && dir.size() >= top.size(); ++guard) {
            DirInfo& d = m_dirs[dir];
            ++d.count;
            if (rank(l) > rank(d.letter)) d.letter = l;
            if (dir == top) break;
            const QString parent = QFileInfo(dir).absolutePath();
            if (parent == dir) break;
            dir = parent;
        }
    }
}

GitExplorerDelegate::Info GitExplorerDelegate::info(const QString& path, bool isDir) const
{
    Info in;
    if (!m_enabled) return in;
    const QString p = QDir::cleanPath(path);
    if (isDir) {
        const auto it = m_dirs.constFind(p);
        if (it != m_dirs.constEnd()) { in.letter = it->letter; in.count = it->count; in.description = describe(it->letter); }
        return in;
    }
    const auto it = m_files.constFind(p);
    if (it != m_files.constEnd()) { in.letter = *it; in.description = describe(*it); return in; }
    // file inside a folder that git lists as one untracked entry
    if (!m_untrackedDirs.isEmpty()) {
        QString dir = QFileInfo(p).absolutePath();
        for (int guard = 0; guard < 64; ++guard) {
            if (m_untrackedDirs.contains(dir)) { in.letter = QLatin1Char('U'); in.description = describe(in.letter); return in; }
            const QString parent = QFileInfo(dir).absolutePath();
            if (parent == dir) break;
            dir = parent;
        }
    }
    return in;
}

void GitExplorerDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const
{
    if (!m_enabled || idx.column() != 0) { QStyledItemDelegate::paint(p, opt, idx); return; }
    const bool dir = m_model->isDir(idx);
    const Info in = info(m_model->filePath(idx), dir);
    if (in.letter.isNull()) { QStyledItemDelegate::paint(p, opt, idx); return; }

    const QColor col = colorFor(in.letter);
    const bool selected = opt.state & QStyle::State_Selected;
    const int badgeW = 22;

    QStyleOptionViewItem o(opt);
    initStyleOption(&o, idx);
    if (!selected) o.palette.setColor(QPalette::Text, col);
    o.rect.setRight(o.rect.right() - badgeW);
    const QWidget* w = opt.widget;
    (w ? w->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &o, p, w);

    p->save();
    const QColor fg = selected ? opt.palette.color(QPalette::HighlightedText) : col;
    if (dir) {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(fg);
        const int d = 7;
        p->drawEllipse(QRectF(opt.rect.right() - 12 - d / 2.0, opt.rect.center().y() - d / 2.0 + 0.5, d, d));
    } else {
        QFont f = opt.font;
        f.setBold(true);
        p->setFont(f);
        p->setPen(fg);
        p->drawText(QRect(opt.rect.right() - badgeW, opt.rect.top(), badgeW - 6, opt.rect.height()),
                    Qt::AlignRight | Qt::AlignVCenter, QString(in.letter));
    }
    p->restore();
}

bool GitExplorerDelegate::helpEvent(QHelpEvent* e, QAbstractItemView* view, const QStyleOptionViewItem& opt, const QModelIndex& idx)
{
    if (m_enabled && e->type() == QEvent::ToolTip && idx.isValid()) {
        const bool dir = m_model->isDir(idx);
        const Info in = info(m_model->filePath(idx), dir);
        if (!in.letter.isNull()) {
            const QString text = dir ? QObject::tr("%1: %n changed file(s)", nullptr, in.count).arg(m_model->fileName(idx))
                                     : QStringLiteral("%1: %2").arg(m_model->fileName(idx), in.description);
            QToolTip::showText(e->globalPos(), text, view);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(e, view, opt, idx);
}
