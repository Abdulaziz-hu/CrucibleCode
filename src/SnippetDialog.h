#pragma once
#include "Snippets.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPlainTextEdit;

// Tools > Snippets...: define keyword triggers with placeholders / tab stops.
class SnippetDialog : public QDialog {
    Q_OBJECT
public:
    explicit SnippetDialog(QWidget* parent = nullptr);

private:
    void accept() override;
    void reloadList(int select = -1);
    void showCurrent();
    void storeCurrent();           // form -> m_items[row]
    int currentIndex() const;

    struct Item { Snippet s; };
    QList<Snippet> m_items;        // built-ins first (read-only), then user snippets
    QListWidget* m_list = nullptr;
    QLineEdit* m_trigger = nullptr;
    QLineEdit* m_desc = nullptr;
    QLineEdit* m_langs = nullptr;
    QPlainTextEdit* m_body = nullptr;
    bool m_loading = false;
};
