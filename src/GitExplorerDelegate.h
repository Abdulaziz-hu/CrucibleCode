#pragma once
#include <QColor>
#include <QHash>
#include <QSet>
#include <QStyledItemDelegate>

class GitManager;
class QFileSystemModel;

// Paints Git status in the Explorer: file names are coloured and get a status letter at the right
// edge (M modified, A added, U untracked, D deleted, R renamed, ! conflict). Folders that contain
// changes get a coloured dot, so changes are visible even when the folder is collapsed.
class GitExplorerDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    struct Info {
        QChar letter;            // null = unchanged
        QString description;     // "Modified", "Untracked", ...
        int count = 0;           // folders: number of changed files inside
    };

    GitExplorerDelegate(GitManager* git, QFileSystemModel* model, QObject* parent = nullptr);

    void setEnabled(bool on) { m_enabled = on; }
    bool enabled() const { return m_enabled; }
    void rebuild();                                         // call after GitManager::statusChanged
    Info info(const QString& absolutePath, bool isDir) const;
    static QColor colorFor(QChar letter);
    static QString describe(QChar letter);

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    bool helpEvent(QHelpEvent* e, QAbstractItemView* view, const QStyleOptionViewItem& opt, const QModelIndex& idx) override;

private:
    struct DirInfo { QChar letter; int count = 0; };
    static int rank(QChar letter);

    GitManager* m_git;
    QFileSystemModel* m_model;
    bool m_enabled = true;
    QHash<QString, QChar> m_files;              // absolute path -> letter
    QHash<QString, DirInfo> m_dirs;             // absolute folder path -> strongest letter below it
    QSet<QString> m_untrackedDirs;              // folders git reports as entirely untracked
};
