#pragma once
#include <QDateTime>
#include <QHash>
#include <QPlainTextEdit>
#include <QSet>
#include <QTextBlock>
#include <QTimer>

#include "LspTypes.h"

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
    ~CodeEditor() override;

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
    void zoomBy(int steps);                       // zoom every editor (one shared, persisted level)
    void resetZoom();                             // back to the font size from the settings
    static qreal globalZoom();                    // shared zoom in points, relative to the settings font
    static void setGlobalZoom(qreal delta);
    static void zoomAll(int steps);
    static void resetZoomAll();

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

    // Language Server integration (driven by LspManager)
    void setLspActive(bool on) { m_lspActive = on; }
    bool lspActive() const { return m_lspActive; }
    void setLspDiagnostics(const QList<LspDiagnostic>& list);
    const QList<LspDiagnostic>& lspDiagnostics() const { return m_lspDiags; }
    void showCompletionItems(const QList<LspCompletionItem>& items);
    void showHoverText(const QString& text);
    QString languageName() const;                 // extension-based id used by the LSP layer

    // snippets (see Snippets.h): Tab expands a trigger word, Tab / Shift+Tab move between tab stops
    bool snippetSessionActive() const { return m_snipActive; }
    bool expandSnippetAtCursor();
    void insertSnippet(const QString& body);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

signals:
    void cursorInfoChanged(int line, int column);
    void lspCompleteRequested(bool manual);       // Ctrl+Space or a trigger character
    void lspDefinitionRequested();                // Ctrl+click

protected:
    void resizeEvent(QResizeEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void changeEvent(QEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    bool viewportEvent(QEvent* e) override;

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

    // LSP state
    void ensureCompleter();
    void refreshExternalPopup();
    bool m_lspActive = false;
    bool m_extMode = false;                       // completer popup currently shows LSP items
    QHash<QString, LspCompletionItem> m_extItems;
    QList<LspDiagnostic> m_lspDiags;
    struct LspMark { QTextCursor cursor; LspDiagnostic d; };
    QList<LspMark> m_lspMarks;
    QList<QTextEdit::ExtraSelection> m_lspSelections;

    // snippet session state
    struct SnipRange { int start = 0, end = 0; };
    struct SnipStop { int index = 0; QList<SnipRange> ranges; };
    bool snipAdjust(int pos, int removed, int added);
    void snipGoto(int stop);
    bool snipMove(bool backwards);
    void endSnippet();
    void snipSyncMirrors();
    void snipUpdateSelections();
    void onContentsChange(int pos, int removed, int added);
    QList<SnipStop> m_snipStops;       // ordered: 1, 2, 3 ... then 0 (the final position)
    SnipRange m_snipWhole;
    int m_snipCur = -1;
    bool m_snipActive = false, m_snipSync = false, m_snipDirty = false;
    QList<QTextEdit::ExtraSelection> m_snipSelections;

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
    static qreal s_zoom;
    static bool s_zoomLoaded;
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
    bool m_wordsDirty = true;

    AiCompletion* m_ai = nullptr;
    bool m_aiOn = false;
    QTimer m_ghostTimer;
    QString m_ghost;
    int m_ghostPos = -1;
    quint64 m_ghostRequest = 0;
};
