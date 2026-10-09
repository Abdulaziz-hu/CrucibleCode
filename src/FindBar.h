#pragma once
#include <QPointer>
#include <QTextDocument>
#include <QWidget>

class CodeEditor;
class QCheckBox;
class QLabel;
class QLineEdit;

// Inline find / replace bar (Ctrl+F / Ctrl+H) operating on the active editor.
class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(QWidget* parent = nullptr);

    void setEditor(CodeEditor* editor);
    void activate(bool withReplace);

public slots:
    void findNext();
    void findPrevious();

private:
    bool doFind(bool backward);
    bool selectionMatches(const QString& selected) const;
    void replaceOne();
    void replaceAll();
    QTextDocument::FindFlags flags() const;

    QPointer<CodeEditor> m_editor;
    QLineEdit* m_find = nullptr;
    QLineEdit* m_replace = nullptr;
    QCheckBox* m_case = nullptr;
    QCheckBox* m_word = nullptr;
    QCheckBox* m_regex = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_replaceRow = nullptr;
};
