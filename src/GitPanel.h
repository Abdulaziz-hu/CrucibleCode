#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QWidget>

class GitManager;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

// Read-only viewer for unified diffs (opened in an editor tab).
class DiffViewer : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit DiffViewer(QWidget* parent = nullptr);
    void setDiff(const QString& text);
};

// "Source Control" panel: branch, staged / unstaged changes, commit box, recent commits.
class GitPanel : public QWidget {
    Q_OBJECT
public:
    GitPanel(GitManager* git, QWidget* parent = nullptr);

signals:
    void openFileRequested(const QString& absolutePath);
    void showDiffRequested(const QString& key, const QString& title, const QString& diffText);

private:
    void rebuild();
    void commitClicked();
    void branchActivated(int index);
    void itemActivated(QTreeWidgetItem* item);
    void contextMenu(const QPoint& pos);
    bool eventFilter(QObject* obj, QEvent* ev) override;

    GitManager* m_git;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_message = nullptr;
    QPushButton* m_initBtn = nullptr;
    QComboBox* m_branch = nullptr;
    QLabel* m_sync = nullptr;
    QPlainTextEdit* m_commitMsg = nullptr;
    QPushButton* m_commitBtn = nullptr;
    QPushButton* m_pullBtn = nullptr;
    QPushButton* m_pushBtn = nullptr;
    QTreeWidget* m_tree = nullptr;
    bool m_commitsExpanded = false;
    bool m_rebuilding = false;
};
