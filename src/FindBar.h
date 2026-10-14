#pragma once
#include <QPointer>
#include <QTextDocument>
#include <QWidget>

class CodeEditor;
class QComboBox;
class QLabel;
class QToolButton;

// Inline find / replace bar (Ctrl+F / Ctrl+H) operating on the active editor.
// The arrow on the left expands the replace row; both fields keep a history dropdown;
// every match is highlighted in the editor while you type.
class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(QWidget* parent = nullptr);

    void setEditor(CodeEditor* editor);
    void activate(bool withReplace);

public slots:
    void findNext();
    void findPrevious();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    bool doFind(bool backward, bool quiet = false);
    void onTextChanged();
    void applyHighlight();
    void updateStatus();
    void setReplaceVisible(bool on);
    void refreshIcons();
    bool selectionMatches(const QString& selected) const;
    void replaceOne();
    void replaceAll();
    void remember(QComboBox* box, const QString& key);
    void loadHistory();
    QString findText() const;
    QString replaceText() const;
    bool useRegex() const;
    QTextDocument::FindFlags flags() const;

    QPointer<CodeEditor> m_editor;
    QToolButton* m_toggle = nullptr;
    QToolButton* m_close = nullptr;
    QComboBox* m_find = nullptr;
    QComboBox* m_replace = nullptr;
    QToolButton* m_case = nullptr;
    QToolButton* m_word = nullptr;
    QToolButton* m_regex = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_replaceRow = nullptr;
};
