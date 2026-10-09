#pragma once
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// "Find in Files" panel: recursive text search over the opened folder.
class SearchPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchPanel(QWidget* parent = nullptr);

    void setRoot(const QString& root, const QString& excludedDir);
    void focusQuery(const QString& seed = {});

signals:
    void openRequested(const QString& path, int line, int column);

private:
    void toggleSearch();
    void runSearch();
    void onItemActivated(QTreeWidgetItem* item);

    QLineEdit* m_query = nullptr;
    QCheckBox* m_case = nullptr;
    QPushButton* m_go = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_results = nullptr;
    QString m_root;
    QString m_excluded;
    bool m_running = false;
    bool m_cancel = false;
};
