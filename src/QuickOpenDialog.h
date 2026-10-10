#pragma once
#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListWidget;

// "Go to File" (Ctrl+P): fuzzy file finder over the opened folder.
class QuickOpenDialog : public QDialog {
    Q_OBJECT
public:
    QuickOpenDialog(const QString& root, QWidget* parent = nullptr);
    QString selectedPath() const { return m_selected; }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void scan();
    void refilter();
    void choose();
    static int fuzzyScore(const QString& path, const QString& query);

    QString m_root, m_selected;
    QStringList m_files;     // relative paths
    QLineEdit* m_edit = nullptr;
    QListWidget* m_list = nullptr;
};
