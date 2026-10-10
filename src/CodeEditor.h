#pragma once
#include <QDateTime>
#include <QPlainTextEdit>
#include <QTextBlock>

class QSyntaxHighlighter;
class LineNumberArea;

// Plain-text code editor: line-number gutter, current-line highlight, smart
// indentation, block (un)indent, comment toggling, CRLF/BOM preserving I/O.
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    bool loadFile(const QString& path, QString* error = nullptr);
    bool saveFile(const QString& path, QString* error = nullptr);

    QString filePath() const { return m_path; }
    bool isUntitled() const { return m_path.isEmpty(); }
    QString displayName() const;
    void setUntitledName(const QString& n) { m_untitled = n; }
    QString eol() const { return m_crlf ? QStringLiteral("CRLF") : QStringLiteral("LF"); }

    void goTo(int line, int column = 1);
    void toggleComment();
    void indentSelection(bool unindent);
    void setEditorFont(const QFont& f);
    void applyTheme();
    void setWordWrap(bool on);
    void setTabSize(int n);
    int tabSize() const { return m_tabSize; }
    bool reloadFile(QString* error = nullptr);   // keeps cursor and scroll position
    QDateTime diskTime() const { return m_diskTime; }
    bool isMarkdown() const;

    // Number of languages provided by KSyntaxHighlighting (0 when it is not built in).
    static int syntaxDefinitionCount();

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

signals:
    void cursorInfoChanged(int line, int column);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();

private:
    void configureLanguage();
    void updateHighlighter();
    void smartNewline();
    void selectedBlocks(QTextBlock* first, QTextBlock* last) const;
    void moveLines(bool up);
    void duplicateLines(bool down);
    void deleteLines();

    LineNumberArea* m_gutter = nullptr;
    QSyntaxHighlighter* m_highlighter = nullptr;
    QString m_path;
    QString m_untitled = QStringLiteral("Untitled");
    QString m_lineComment;
    bool m_colonIndent = false;
    int m_tabSize = 4;
    QDateTime m_diskTime;
    bool m_crlf = false;
    bool m_bom = false;
};
