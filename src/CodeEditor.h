#pragma once
#include <QDateTime>
#include <QHash>
#include <QPlainTextEdit>
#include <QSet>
#include <QTextBlock>
#include <QTimer>

class AiCompletion;
class QCompleter;
class QStringListModel;
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
    bool isWebFile() const;                       // html / css / js ...: shows the Live Preview button
    void setCaretBlinkMode(int mode);             // 0 = solid while typing, blink when idle; 1 = always blink; 2 = never
    void zoomBy(int steps);                       // our own zoom: survives theme / settings changes
    void resetZoom();                             // back to the font size from the settings

    // find: highlight every match (as you type in the Find bar)
    void setSearchHighlight(const QString& text, bool caseSensitive, bool wholeWord, bool regex);
    void clearSearchHighlight();
    int searchMatchCount() const { return int(m_searchRanges.size()); }
    int searchIndexAt(int position) const;        // 1-based index of the match starting at `position`, 0 if none

    // Git change markers in the gutter (line numbers are 1-based)
    enum MarkKind { MarkAdded = 1, MarkModified = 2, MarkDeleted = 3 };
    void setLineMarks(const QHash<int, int>& marks);

    // optional helpers (all off by default)
    void setSuggestionsEnabled(bool on);          // word / keyword completion popup
    void setSpellCheckEnabled(bool on);           // red underline in Markdown / text files
    void setAiCompletion(AiCompletion* ai, bool on);   // grey "ghost" suggestion, Tab to accept

    // Number of languages provided by KSyntaxHighlighting (0 when it is not built in).
    static int syntaxDefinitionCount();

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

signals:
    void cursorInfoChanged(int line, int column);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void changeEvent(QEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

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
    void editKey(QKeyEvent* e);
    void applyFont();
    QFont zoomedFont() const;
    void rerunSearch();
    void caretSolid();
    void autoCloseTag();
    void rebuildExtraSelections();
    // spell check
    bool spellCheckApplies() const;
    void runSpellCheck();
    // completion popup
    void rebuildWords();
    void showCompletion(bool force);
    QString wordPrefixAtCursor(int* start) const;
    void insertCompletion(const QString& text);
    // AI ghost text
    void requestGhost();
    void clearGhost();
    void acceptGhost();
    QString textRange(int from, int to) const;

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

    QFont m_baseFont;                                   // font from the settings
    qreal m_zoomDelta = 0;                              // zoom in points, relative to m_baseFont
    bool m_applyingFont = false;

    QList<QTextEdit::ExtraSelection> m_searchSelections;
    QList<QPair<int, int>> m_searchRanges;              // [start, end) of every match
    QString m_searchText;
    bool m_searchCase = false, m_searchWhole = false, m_searchRegex = false;
    QTimer m_searchTimer;
    QHash<int, int> m_lineMarks;
    QList<QTextEdit::ExtraSelection> m_lineSelections, m_bracketSelections, m_spellSelections;
    QList<QPair<int, int>> m_spellRanges;               // [start, end) of underlined words

    // own caret so it can stay solid while typing
    QTimer m_caretBlink, m_caretIdle;
    bool m_caretOn = true;
    int m_caretMode = 0;
    bool m_markup = false;                              // html / xml: auto-close tags

    bool m_spell = false;
    QTimer m_spellTimer;

    bool m_suggest = false;
    QCompleter* m_completer = nullptr;
    QStringListModel* m_wordsModel = nullptr;
    QStringList m_langWords;
    QTimer m_wordsTimer;

    AiCompletion* m_ai = nullptr;
    bool m_aiOn = false;
    QTimer m_ghostTimer;
    QString m_ghost;
    int m_ghostPos = -1;
    quint64 m_ghostRequest = 0;
};
