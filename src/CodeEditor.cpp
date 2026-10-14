#include "CodeEditor.h"
#include "AiCompletion.h"
#include "SpellChecker.h"
#include "SyntaxHighlighter.h"
#include "ThemeManager.h"

#include <climits>

#include <QAbstractItemView>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QFile>
#include <QMenu>
#include <QRegularExpression>
#include <QStringListModel>
#include <QWheelEvent>
#include <QFontMetricsF>
#include <QPaintEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QToolTip>
#include <QMouseEvent>
#include <QSettings>
#include <QTextCursor>

#ifdef CRUCIBLE_HAVE_KSYNTAX
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif

#ifdef CRUCIBLE_HAVE_KSYNTAX
static KSyntaxHighlighting::Repository& syntaxRepository()
{
    static KSyntaxHighlighting::Repository repo;
    return repo;
}
#endif

int CodeEditor::syntaxDefinitionCount()
{
#ifdef CRUCIBLE_HAVE_KSYNTAX
    return int(syntaxRepository().definitions().size());
#else
    return 0;
#endif
}

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* e) : QWidget(e), m_editor(e) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent* ev) override { m_editor->lineNumberAreaPaintEvent(ev); }

private:
    CodeEditor* m_editor;
};

// ---- global zoom -------------------------------------------------------------------------------
// One zoom level (in points, relative to the font from the settings) is shared by every editor
// and persisted, so tabs, newly opened files and later sessions all use the same text size.
static QList<CodeEditor*>& allEditors()
{
    static QList<CodeEditor*> list;
    return list;
}

qreal CodeEditor::s_zoom = 0;
bool CodeEditor::s_zoomLoaded = false;

qreal CodeEditor::globalZoom()
{
    if (!s_zoomLoaded) {
        s_zoom = QSettings().value(QStringLiteral("editor/zoomDelta"), 0.0).toDouble();
        s_zoomLoaded = true;
    }
    return s_zoom;
}

void CodeEditor::setGlobalZoom(qreal delta)
{
    delta = qBound<qreal>(-30.0, delta, 60.0);
    s_zoom = delta;
    s_zoomLoaded = true;
    QSettings().setValue(QStringLiteral("editor/zoomDelta"), delta);
    for (CodeEditor* e : allEditors()) e->applyFont();
}

void CodeEditor::zoomAll(int steps) { setGlobalZoom(globalZoom() + steps); }
void CodeEditor::resetZoomAll() { setGlobalZoom(0); }

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    allEditors().append(this);
    setObjectName(QStringLiteral("codeEditor"));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameStyle(QFrame::NoFrame);
    setCursorWidth(0);   // the native caret is replaced by our own (see paintEvent): no blinking while typing

    m_gutter = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        const QTextCursor c = textCursor();
        emit cursorInfoChanged(c.blockNumber() + 1, c.positionInBlock() + 1);
    });

    m_caretBlink.setInterval(530);
    connect(&m_caretBlink, &QTimer::timeout, this, [this] {
        m_caretOn = !m_caretOn;
        viewport()->update();
    });
    m_caretIdle.setSingleShot(true);
    m_caretIdle.setInterval(600);
    connect(&m_caretIdle, &QTimer::timeout, this, [this] { if (hasFocus() && m_caretMode == 0) m_caretBlink.start(); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { if (m_caretMode == 0) caretSolid(); });

    m_searchTimer.setSingleShot(true);
    connect(&m_searchTimer, &QTimer::timeout, this, &CodeEditor::rerunSearch);
    connect(this, &QPlainTextEdit::textChanged, this, [this] { if (!m_searchText.isEmpty()) m_searchTimer.start(150); });

    m_spellTimer.setSingleShot(true);
    connect(&m_spellTimer, &QTimer::timeout, this, &CodeEditor::runSpellCheck);
    connect(this, &QPlainTextEdit::textChanged, this, [this] { if (m_spell) m_spellTimer.start(350); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (m_spell) m_spellTimer.start(120); });

    m_wordsTimer.setSingleShot(true);
    connect(&m_wordsTimer, &QTimer::timeout, this, &CodeEditor::rebuildWords);
    connect(this, &QPlainTextEdit::textChanged, this, [this] { if (m_suggest) m_wordsTimer.start(600); });

    m_ghostTimer.setSingleShot(true);
    connect(&m_ghostTimer, &QTimer::timeout, this, &CodeEditor::requestGhost);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (!m_ghost.isEmpty() && textCursor().position() != m_ghostPos) clearGhost();
    });

    connect(document(), &QTextDocument::contentsChange, this, &CodeEditor::onContentsChange);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (m_snipActive && !m_snipSync) {
            const int p = textCursor().position();
            if (p < m_snipWhole.start || p > m_snipWhole.end) endSnippet();
        }
    });

    updateLineNumberAreaWidth();
    applyTheme();
}

CodeEditor::~CodeEditor() { allEditors().removeAll(this); }

QString CodeEditor::displayName() const
{
    return m_path.isEmpty() ? m_untitled : QFileInfo(m_path).fileName();
}

void CodeEditor::setTabSize(int n)
{
    m_tabSize = qBound(1, n, 16);
    setTabStopDistance(m_tabSize * QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')));
}

bool CodeEditor::reloadFile(QString* error)
{
    const int pos = textCursor().position();
    const int scroll = verticalScrollBar()->value();
    const QString path = m_path;
    if (!loadFile(path, error)) return false;
    QTextCursor c(document());
    c.setPosition(qMin(pos, qMax(0, document()->characterCount() - 1)));
    setTextCursor(c);
    verticalScrollBar()->setValue(scroll);
    return true;
}

void CodeEditor::setWordWrap(bool on)
{
    setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

bool CodeEditor::isWebFile() const
{
    static const QStringList web = {"html", "htm", "xhtml", "css", "js", "mjs", "cjs", "json", "svg", "jsx"};
    return !m_path.isEmpty() && web.contains(QFileInfo(m_path).suffix().toLower());
}

void CodeEditor::setCaretBlinkMode(int mode)
{
    m_caretMode = qBound(0, mode, 2);
    caretSolid();
    if (m_caretMode == 1 && hasFocus()) m_caretBlink.start();
}

// Caret visible and steady; in mode 0 it starts blinking again once typing has paused.
void CodeEditor::caretSolid()
{
    m_caretOn = true;
    m_caretBlink.stop();
    if (m_caretMode == 0 && hasFocus()) m_caretIdle.start();
    viewport()->update();
}

void CodeEditor::focusInEvent(QFocusEvent* e)
{
    QPlainTextEdit::focusInEvent(e);
    caretSolid();
    if (m_caretMode == 1) m_caretBlink.start();
}

void CodeEditor::focusOutEvent(QFocusEvent* e)
{
    QPlainTextEdit::focusOutEvent(e);
    m_caretBlink.stop();
    m_caretIdle.stop();
    viewport()->update();
}

// Typing  <div>  inserts  </div>  after the caret.
void CodeEditor::autoCloseTag()
{
    if (!m_markup) return;
    QTextCursor c = textCursor();
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    if (col < 2 || line.at(col - 1) != QLatin1Char('>')) return;
    const int lt = int(line.lastIndexOf(QLatin1Char('<'), col - 1));
    if (lt < 0) return;
    const QString inner = line.mid(lt + 1, col - lt - 2);
    if (inner.isEmpty() || inner.startsWith(QLatin1Char('/')) || inner.startsWith(QLatin1Char('!'))
        || inner.startsWith(QLatin1Char('?')) || inner.endsWith(QLatin1Char('/')))
        return;
    static const QRegularExpression nameRe(QStringLiteral("^([A-Za-z][A-Za-z0-9:_.-]*)"));
    const auto m = nameRe.match(inner);
    if (!m.hasMatch()) return;
    const QString name = m.captured(1);
    static const QSet<QString> voids = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta",
                                        "param", "source", "track", "wbr", "path", "circle", "rect", "line", "stop"};
    if (voids.contains(name.toLower())) return;
    if (line.mid(col).startsWith(QStringLiteral("</") + name)) return;
    c.insertText(QStringLiteral("</") + name + QLatin1Char('>'));
    c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, int(name.size()) + 3);
    setTextCursor(c);
}

bool CodeEditor::isMarkdown() const
{
    const QString ext = QFileInfo(m_path).suffix().toLower();
    return ext == QLatin1String("md") || ext == QLatin1String("markdown") || ext == QLatin1String("mdown")
        || ext == QLatin1String("mkd") || ext == QLatin1String("mdx");
}

QFont CodeEditor::zoomedFont() const
{
    QFont f = m_baseFont;
    if (f.pointSizeF() > 0) f.setPointSizeF(qMax<qreal>(2.0, f.pointSizeF() + globalZoom()));
    return f;
}

void CodeEditor::applyFont()
{
    m_applyingFont = true;
    const QFont f = zoomedFont();
    setFont(f);
    setTabStopDistance(m_tabSize * QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')));
    m_applyingFont = false;
    updateLineNumberAreaWidth();
    m_gutter->update();
}

void CodeEditor::setEditorFont(const QFont& f)
{
    m_baseFont = f;          // the zoom level (m_zoomDelta) is kept
    applyFont();
}

void CodeEditor::zoomBy(int steps)
{
    if (m_baseFont.pointSizeF() <= 0) return;
    const qreal lowest = 2.0 - m_baseFont.pointSizeF();      // never smaller than 2 pt
    setGlobalZoom(qMax(lowest, globalZoom() + steps));        // applies to all open editors
}

void CodeEditor::resetZoom() { resetZoomAll(); }

// A style sheet or theme change can reset the widget font behind our back: put the zoomed font back.
void CodeEditor::changeEvent(QEvent* e)
{
    QPlainTextEdit::changeEvent(e);
    if (m_applyingFont || m_baseFont.pointSizeF() <= 0) return;
    if (e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange || e->type() == QEvent::ApplicationFontChange) {
        const QFont want = zoomedFont();
        if (qAbs(font().pointSizeF() - want.pointSizeF()) > 0.01 || font().family() != want.family()) applyFont();
    }
}

void CodeEditor::applyTheme()
{
    const ThemeColors& c = ThemeManager::instance().colors();
    QPalette p = palette();
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Highlight, c.selection);
    p.setBrush(QPalette::HighlightedText, QBrush(Qt::NoBrush)); // keep syntax colors when selected
    setPalette(p);
    updateHighlighter();
    highlightCurrentLine();
    m_gutter->update();
}

// ---- language configuration ---------------------------------------------------

void CodeEditor::configureLanguage()
{
    const QFileInfo fi(m_path);
    const QString ext = fi.suffix().toLower();
    const QString name = fi.fileName().toLower();

    static const QStringList slash = {"c","cc","cpp","cxx","h","hh","hpp","hxx","inl","ipp","cs",
        "java","js","jsx","ts","tsx","go","rs","swift","kt","kts","scala","php","dart","mm","m",
        "cu","glsl","vert","frag","json5","jsonc","proto","qml","ino","zig","d"};
    static const QStringList hash = {"py","pyw","sh","bash","zsh","rb","pl","yml","yaml","toml",
        "cmake","ini","conf","r","ps1","mk","cfg","gitignore","dockerfile"};
    static const QStringList dashes = {"sql","lua","hs","ada","vhdl"};

    m_lineComment.clear();
    if (slash.contains(ext)) m_lineComment = QStringLiteral("//");
    else if (hash.contains(ext) || name == "cmakelists.txt" || name == "makefile")
        m_lineComment = QStringLiteral("#");
    else if (dashes.contains(ext)) m_lineComment = QStringLiteral("--");
    m_colonIndent = (ext == "py" || ext == "pyw" || ext == "yml" || ext == "yaml");
    m_markup = (ext == "html" || ext == "htm" || ext == "xhtml" || ext == "xml" || ext == "svg" || ext == "xaml"
                || ext == "vue" || ext == "jsx" || ext == "tsx" || ext == "axaml");
    m_langWords = SyntaxHighlighter::completionWords(m_path);
    if (m_suggest) m_wordsTimer.start(50);
    if (m_spell) m_spellTimer.start(200);
}

void CodeEditor::updateHighlighter()
{
    delete m_highlighter;
    m_highlighter = nullptr;

#ifdef CRUCIBLE_HAVE_KSYNTAX
    if (!m_path.isEmpty()) {
        KSyntaxHighlighting::Repository& repo = syntaxRepository();
        const auto def = repo.definitionForFileName(m_path);
        if (def.isValid()) {
            auto* h = new KSyntaxHighlighting::SyntaxHighlighter(document());
            h->setDefinition(def);
            h->setTheme(repo.defaultTheme(ThemeManager::instance().isDark()
                            ? KSyntaxHighlighting::Repository::DarkTheme
                            : KSyntaxHighlighting::Repository::LightTheme));
            m_highlighter = h;
            return;
        }
    }
#endif
    m_highlighter = new SyntaxHighlighter(document(), m_path);
}

// ---- file I/O ---------------------------------------------------------------------

bool CodeEditor::loadFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QByteArray data = f.readAll();
    if (data.left(8192).contains('\0')) {
        if (error) *error = tr("This file appears to be binary and cannot be edited as text.");
        return false;
    }
    m_bom = data.startsWith("\xEF\xBB\xBF");
    if (m_bom) data.remove(0, 3);
    m_crlf = data.contains("\r\n");
    if (m_crlf) data.replace("\r\n", "\n");

    m_path = QFileInfo(path).absoluteFilePath();
    configureLanguage();
    setPlainText(QString::fromUtf8(data));
    document()->setModified(false);
    m_diskTime = QFileInfo(m_path).lastModified();
    updateHighlighter();
    moveCursor(QTextCursor::Start);
    return true;
}

bool CodeEditor::saveFile(const QString& path, QString* error)
{
    QString text = toPlainText();
    if (m_crlf) text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (m_bom) f.write("\xEF\xBB\xBF", 3);
    f.write(text.toUtf8());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (abs != m_path) {
        m_path = abs;
        configureLanguage();
        updateHighlighter();
    }
    document()->setModified(false);
    m_diskTime = QFileInfo(m_path).lastModified();
    return true;
}

// ---- navigation ---------------------------------------------------------------------

void CodeEditor::goTo(int line, int column)
{
    const QTextBlock b = document()->findBlockByNumber(qMax(0, line - 1));
    if (!b.isValid()) return;
    QTextCursor c(b);
    c.setPosition(b.position() + qBound(0, column - 1, qMax(0, int(b.length()) - 1)));
    setTextCursor(c);
    centerCursor();
    setFocus();
}

// ---- line number gutter ---------------------------------------------------------------

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 3;
    for (int max = blockCount(); max >= 1000; max /= 10) ++digits;
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        m_gutter->scroll(0, dy);
    else
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    const ThemeColors& c = ThemeManager::instance().colors();
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), c.gutterBg);
    painter.setPen(c.border);
    painter.drawLine(m_gutter->width() - 1, event->rect().top(), m_gutter->width() - 1, event->rect().bottom());

    const int current = textCursor().blockNumber();
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setFont(font());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(number == current ? c.gutterActive : c.gutterFg);
            painter.drawText(0, top, m_gutter->width() - 7, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number + 1));
            const int mark = m_lineMarks.value(number + 1, 0);
            if (mark == MarkAdded || mark == MarkModified) {
                painter.fillRect(1, top, 3, bottom - top, mark == MarkAdded ? c.gitAdded : c.gitModified);
            } else if (mark == MarkDeleted) {
                QPolygon tri;
                tri << QPoint(1, top - 3) << QPoint(7, top) << QPoint(1, top + 3);
                painter.setPen(Qt::NoPen);
                painter.setBrush(c.gitDeleted);
                painter.drawPolygon(tri);
                painter.setBrush(Qt::NoBrush);
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::highlightCurrentLine()
{
    const ThemeColors& tc = ThemeManager::instance().colors();
    m_lineSelections.clear();
    m_bracketSelections.clear();
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(tc.lineHighlight);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        m_lineSelections.append(sel);
    }

    // Bracket matching: the bracket before or after the caret and its partner.
    QTextDocument* doc = document();
    const int pos = textCursor().position();
    const int count = doc->characterCount();
    const auto charAt = [&](int p) { return (p >= 0 && p < count) ? doc->characterAt(p) : QChar(); };
    static const QString opens = QStringLiteral("([{"), closes = QStringLiteral(")]}");
    const auto isBracket = [&](QChar c) { return !c.isNull() && (opens.contains(c) || closes.contains(c)); };

    int at = -1;
    if (isBracket(charAt(pos))) at = pos;
    else if (isBracket(charAt(pos - 1))) at = pos - 1;
    if (at >= 0) {
        const QChar b = charAt(at);
        const bool forward = opens.contains(b);
        const QChar mate = forward ? closes.at(int(opens.indexOf(b))) : opens.at(int(closes.indexOf(b)));
        const int step = forward ? 1 : -1;
        int depth = 0, match = -1;
        for (int i = at, n = 0; i >= 0 && i < count && n < 200000; i += step, ++n) {
            const QChar ch = charAt(i);
            if (ch == b) ++depth;
            else if (ch == mate && --depth == 0) { match = i; break; }
        }
        const auto mark = [&](int p, bool ok) {
            QTextEdit::ExtraSelection s;
            s.cursor = QTextCursor(doc);
            s.cursor.setPosition(p);
            s.cursor.setPosition(p + 1, QTextCursor::KeepAnchor);
            if (ok) s.format.setBackground(tc.selection);
            else s.format.setForeground(tc.error);
            m_bracketSelections.append(s);
        };
        mark(at, match >= 0);
        if (match >= 0) mark(match, true);
    }
    rebuildExtraSelections();
}

void CodeEditor::rebuildExtraSelections()
{
    QList<QTextEdit::ExtraSelection> all = m_lineSelections;
    all += m_searchSelections;
    all += m_bracketSelections;
    all += m_spellSelections;
    all += m_snipSelections;
    all += m_lspSelections;
    setExtraSelections(all);
}

// ---- editing helpers ----------------------------------------------------------------------

void CodeEditor::selectedBlocks(QTextBlock* first, QTextBlock* last) const
{
    const QTextCursor c = textCursor();
    const int s = c.selectionStart();
    const int e = c.selectionEnd();
    *first = document()->findBlock(s);
    *last = document()->findBlock(e);
    if (e > s && last->position() == e)
        *last = last->previous();
}

void CodeEditor::indentSelection(bool unindent)
{
    QTextBlock first, last;
    selectedBlocks(&first, &last);
    QTextCursor edit = textCursor();
    edit.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QTextCursor bc(b);
        if (!unindent) {
            if (!b.text().isEmpty()) bc.insertText(QString(m_tabSize, QLatin1Char(' ')));
        } else {
            const QString t = b.text();
            int n = 0;
            if (t.startsWith(QLatin1Char('\t'))) n = 1;
            else while (n < m_tabSize && n < t.size() && t.at(n) == QLatin1Char(' ')) ++n;
            if (n > 0) {
                bc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, n);
                bc.removeSelectedText();
            }
        }
        if (b == last) break;
    }
    edit.endEditBlock();
}

void CodeEditor::toggleComment()
{
    if (m_lineComment.isEmpty()) return;
    QTextBlock first, last;
    selectedBlocks(&first, &last);

    bool allCommented = true, any = false;
    int minIndent = INT_MAX;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const QString t = b.text();
        if (!t.trimmed().isEmpty()) {
            any = true;
            int ind = 0;
            while (ind < t.size() && t.at(ind).isSpace()) ++ind;
            minIndent = qMin(minIndent, ind);
            if (!QStringView(t).mid(ind).startsWith(m_lineComment)) allCommented = false;
        }
        if (b == last) break;
    }
    if (!any) return;

    QTextCursor edit = textCursor();
    edit.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const QString t = b.text();
        if (!t.trimmed().isEmpty()) {
            QTextCursor bc(b);
            if (allCommented) {
                int ind = 0;
                while (ind < t.size() && t.at(ind).isSpace()) ++ind;
                int n = int(m_lineComment.size());
                if (QStringView(t).mid(ind + n).startsWith(QLatin1Char(' '))) ++n;
                bc.setPosition(b.position() + ind);
                bc.setPosition(b.position() + ind + n, QTextCursor::KeepAnchor);
                bc.removeSelectedText();
            } else {
                bc.setPosition(b.position() + minIndent);
                bc.insertText(m_lineComment + QLatin1Char(' '));
            }
        }
        if (b == last) break;
    }
    edit.endEditBlock();
}

void CodeEditor::moveLines(bool up)
{
    QTextBlock first, last;
    selectedBlocks(&first, &last);
    const QTextBlock neighbor = up ? first.previous() : last.next();
    if (!neighbor.isValid()) return;

    const int rangeStart = first.position();
    const int rangeEnd = last.position() + last.length() - 1;
    QTextCursor r(document());
    r.setPosition(rangeStart);
    r.setPosition(rangeEnd, QTextCursor::KeepAnchor);
    QString rangeText = r.selectedText();
    rangeText.replace(QChar(0x2029), QLatin1Char('\n'));
    const QString nb = neighbor.text();

    const int regionStart = up ? neighbor.position() : rangeStart;
    const int regionEnd = up ? rangeEnd : neighbor.position() + neighbor.length() - 1;
    const QString replacement = up ? rangeText + QLatin1Char('\n') + nb : nb + QLatin1Char('\n') + rangeText;
    const int newStart = up ? regionStart : regionStart + int(nb.size()) + 1;

    QTextCursor e(document());
    e.beginEditBlock();
    e.setPosition(regionStart);
    e.setPosition(regionEnd, QTextCursor::KeepAnchor);
    e.insertText(replacement);
    e.endEditBlock();

    QTextCursor sel(document());
    sel.setPosition(newStart);
    sel.setPosition(newStart + int(rangeText.size()), QTextCursor::KeepAnchor);
    setTextCursor(sel);
}

void CodeEditor::duplicateLines(bool down)
{
    QTextBlock first, last;
    selectedBlocks(&first, &last);
    const int rangeStart = first.position();
    const int rangeEnd = last.position() + last.length() - 1;
    QTextCursor r(document());
    r.setPosition(rangeStart);
    r.setPosition(rangeEnd, QTextCursor::KeepAnchor);
    QString text = r.selectedText();
    text.replace(QChar(0x2029), QLatin1Char('\n'));

    QTextCursor e(document());
    e.beginEditBlock();
    if (down) {
        e.setPosition(rangeEnd);
        e.insertText(QLatin1Char('\n') + text);
    } else {
        e.setPosition(rangeStart);
        e.insertText(text + QLatin1Char('\n'));
    }
    e.endEditBlock();
}

void CodeEditor::deleteLines()
{
    QTextBlock first, last;
    selectedBlocks(&first, &last);
    int from = first.position();
    int to = last.position() + last.length() - 1;      // end of last line (without its newline)
    if (last.next().isValid()) to = last.next().position();   // take the newline after the lines
    else if (first.previous().isValid()) from = first.position() - 1;   // last line: take the newline before
    QTextCursor c(document());
    c.beginEditBlock();
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.endEditBlock();
}

void CodeEditor::smartNewline()
{
    QTextCursor c = textCursor();
    const QString line = c.block().text();
    const int pos = c.positionInBlock();

    QString indent;
    for (const QChar ch : line) {
        if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t')) indent += ch;
        else break;
    }
    if (indent.size() > pos) indent = indent.left(pos);

    const QString before = line.left(pos).trimmed();
    const QString after = line.mid(pos).trimmed();
    const bool open = before.endsWith(QLatin1Char('{')) || before.endsWith(QLatin1Char('('))
                   || before.endsWith(QLatin1Char('[')) || (m_colonIndent && before.endsWith(QLatin1Char(':')));
    const bool closeFollows = after.startsWith(QLatin1Char('}')) || after.startsWith(QLatin1Char(')'))
                           || after.startsWith(QLatin1Char(']'));

    c.beginEditBlock();
    c.insertText(QLatin1Char('\n') + indent + (open ? QString(m_tabSize, QLatin1Char(' ')) : QString()));
    if (open && closeFollows) {
        const int p = c.position();
        c.insertText(QLatin1Char('\n') + indent);
        c.setPosition(p);
    }
    c.endEditBlock();
    setTextCursor(c);
    ensureCursorVisible();
}

void CodeEditor::editKey(QKeyEvent* e)
{
    const bool plain = !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool onlyAlt = (mods & Qt::AltModifier) && !(mods & (Qt::ControlModifier | Qt::MetaModifier));

    if (!isReadOnly()) {
        if (onlyAlt && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
            if (mods & Qt::ShiftModifier) duplicateLines(e->key() == Qt::Key_Down);   // Shift+Alt+Up/Down
            else moveLines(e->key() == Qt::Key_Up);                                  // Alt+Up/Down
            return;
        }
        if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && e->key() == Qt::Key_K) {
            deleteLines();                                                           // Ctrl+Shift+K
            return;
        }
    }

    if (m_snipActive && !isReadOnly()) {
        if (e->key() == Qt::Key_Tab && plain) { if (snipMove(false)) return; }
        if (e->key() == Qt::Key_Backtab) { if (snipMove(true)) return; }
        if (e->key() == Qt::Key_Escape) { endSnippet(); return; }
    }
    if (e->key() == Qt::Key_Tab && plain && !isReadOnly() && !m_snipActive && expandSnippetAtCursor()) return;
    if (e->key() == Qt::Key_Tab && plain) {
        const QTextCursor c = textCursor();
        if (c.hasSelection() && document()->findBlock(c.selectionStart()) != document()->findBlock(c.selectionEnd()))
            indentSelection(false);
        else
            insertPlainText(QString(m_tabSize - c.positionInBlock() % m_tabSize, QLatin1Char(' ')));
        return;
    }
    if (e->key() == Qt::Key_Backtab) {
        indentSelection(true);
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && plain && !isReadOnly()) {
        smartNewline();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}


// ---------------------------------------------------------------------------------------------
//  Key handling wrapper: completion popup, AI ghost text, then the normal editing keys
// ---------------------------------------------------------------------------------------------

void CodeEditor::keyPressEvent(QKeyEvent* e)
{
    if (m_completer && m_completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Escape: case Qt::Key_Tab: case Qt::Key_Backtab:
            e->ignore();     // the completer handles these
            return;
        default: break;
        }
    }
    if (!m_ghost.isEmpty()) {
        if (e->key() == Qt::Key_Tab && e->modifiers() == Qt::NoModifier) { acceptGhost(); return; }
        if (e->key() == Qt::Key_Escape) { clearGhost(); return; }
    }
    if (e->key() == Qt::Key_Space && (e->modifiers() & Qt::ControlModifier)) {
        if (m_lspActive) { emit lspCompleteRequested(true); return; }
        if (m_suggest) { showCompletion(true); return; }
    }
    if (m_extMode && (!m_completer || !m_completer->popup()->isVisible())) {      // popup was dismissed
        m_extMode = false;
        if (m_suggest) rebuildWords();
    }

    if (m_caretMode == 0) caretSolid();
    const QString typed = e->text();
    editKey(e);
    if (typed == QLatin1String(">")) autoCloseTag();

    const bool wordChar = !typed.isEmpty() && (typed.at(0).isLetterOrNumber() || typed.at(0) == QLatin1Char('_'));
    bool handledExt = false;
    if (m_extMode && m_completer && m_completer->popup()->isVisible()) {
        handledExt = true;
        if (wordChar || e->key() == Qt::Key_Backspace) refreshExternalPopup();
        else { m_completer->popup()->hide(); m_extMode = false; handledExt = false; if (m_suggest) rebuildWords(); }
    }
    if (m_lspActive && !isReadOnly()) {
        const QString before = textCursor().block().text().left(textCursor().positionInBlock());
        if (typed == QLatin1String(".") || (typed == QLatin1String(">") && before.endsWith(QLatin1String("->")))
            || (typed == QLatin1String(":") && before.endsWith(QLatin1String("::")))) emit lspCompleteRequested(false);
    }
    if (!handledExt && m_suggest && m_completer && !m_extMode) {
        if (wordChar || e->key() == Qt::Key_Backspace) showCompletion(false);
        else m_completer->popup()->hide();
    }
    clearGhost();
    if (m_aiOn && !typed.isEmpty() && typed.at(0).unicode() >= 0x20) m_ghostTimer.start(450);
    else if (m_aiOn && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) m_ghostTimer.start(450);
    else if (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete) m_ghostTimer.stop();
}

// ---------------------------------------------------------------------------------------------
//  Shift + wheel scrolls sideways
// ---------------------------------------------------------------------------------------------

void CodeEditor::wheelEvent(QWheelEvent* e)
{
    if ((e->modifiers() & Qt::ShiftModifier) && !(e->modifiers() & Qt::ControlModifier)) {
        int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
        if (delta == 0) delta = e->pixelDelta().y() != 0 ? e->pixelDelta().y() : e->pixelDelta().x();
        if (delta != 0) {
            QScrollBar* h = horizontalScrollBar();
            h->setValue(h->value() - delta);
        }
        e->accept();
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {   // Ctrl + wheel zooms
        const int dy = e->angleDelta().y();
        if (dy != 0) zoomBy(dy > 0 ? 1 : -1);
        e->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

// ---------------------------------------------------------------------------------------------
//  Spell check (Markdown and plain text files)
// ---------------------------------------------------------------------------------------------

void CodeEditor::setSpellCheckEnabled(bool on)
{
    m_spell = on;
    if (on) m_spellTimer.start(100);
    else { m_spellSelections.clear(); m_spellRanges.clear(); rebuildExtraSelections(); }
}

bool CodeEditor::spellCheckApplies() const
{
    if (m_path.isEmpty()) return false;
    const QFileInfo fi(m_path);
    static const QStringList exts = {"md", "markdown", "mdown", "mkd", "mdx", "txt", "text", "rst", "adoc", "tex"};
    static const QStringList names = {"readme", "license", "licence", "changelog", "notice", "authors", "contributing"};
    const QString ext = fi.suffix().toLower();
    return exts.contains(ext) || (ext.isEmpty() && names.contains(fi.fileName().toLower()));
}

void CodeEditor::runSpellCheck()
{
    m_spellSelections.clear();
    m_spellRanges.clear();
    if (!m_spell || !spellCheckApplies()) { rebuildExtraSelections(); return; }

    const bool markdown = isMarkdown();
    static const QRegularExpression fence(QStringLiteral("^\\s*(```|~~~)"));
    static const QRegularExpression maskRe(QStringLiteral(
        "`[^`\\n]*`|\\]\\([^)\\n]*\\)|https?://\\S+|www\\.\\S+|<[^>\\n]+>|\\S+@\\S+\\.\\S+|\\[[^\\]\\n]*\\]:\\s*\\S+"));
    static const QRegularExpression wordRe(QStringLiteral("[A-Za-z]+(?:['\\x{2019}][A-Za-z]+)*"));

    // Is the first visible line inside a fenced code block?
    bool inFence = false;
    const QTextBlock firstVisible = firstVisibleBlock();
    if (markdown) {
        for (QTextBlock b = document()->begin(); b.isValid() && b != firstVisible; b = b.next())
            if (fence.match(b.text()).hasMatch()) inFence = !inFence;
    }

    const ThemeColors& tc = ThemeManager::instance().colors();
    SpellChecker& sc = SpellChecker::instance();
    const int cursorPos = textCursor().position();
    const int bottom = viewport()->height();

    for (QTextBlock b = firstVisible; b.isValid(); b = b.next()) {
        const QRectF r = blockBoundingGeometry(b).translated(contentOffset());
        if (r.top() > bottom) break;
        if (!b.isVisible()) continue;

        QString text = b.text();
        if (markdown) {
            if (fence.match(text).hasMatch()) { inFence = !inFence; continue; }
            if (inFence) continue;
            if (text.startsWith(QStringLiteral("    ")) || text.startsWith(QLatin1Char('\t'))) continue;   // indented code
        }
        // blank out things that are not prose
        auto it = maskRe.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            for (qsizetype i = m.capturedStart(); i < m.capturedEnd(); ++i) text[i] = QLatin1Char(' ');
        }

        auto wi = wordRe.globalMatch(text);
        while (wi.hasNext()) {
            const auto m = wi.next();
            const QString word = m.captured();
            const int s = int(m.capturedStart());
            const int e = int(m.capturedEnd());
            if (word.size() < 3) continue;
            if (s > 0 && (text.at(s - 1).isDigit() || text.at(s - 1) == QLatin1Char('_'))) continue;
            if (e < text.size() && (text.at(e).isDigit() || text.at(e) == QLatin1Char('_'))) continue;
            // CamelCase, ALLCAPS and mixed-case identifiers are not prose
            bool mixed = false;
            for (int i = 1; i < word.size(); ++i) if (word.at(i).isUpper() && word.at(i - 1) != QLatin1Char('\'')) { mixed = true; break; }
            if (mixed) continue;
            if (sc.isCorrect(word)) continue;

            const int absStart = b.position() + s, absEnd = b.position() + e;
            m_spellRanges.append(qMakePair(absStart, absEnd));
            if (cursorPos >= absStart && cursorPos <= absEnd && hasFocus()) continue;   // still typing this word

            QTextEdit::ExtraSelection sel;
            sel.cursor = QTextCursor(document());
            sel.cursor.setPosition(absStart);
            sel.cursor.setPosition(absEnd, QTextCursor::KeepAnchor);
            sel.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            sel.format.setUnderlineColor(tc.error);
            m_spellSelections.append(sel);
        }
    }
    rebuildExtraSelections();
}

void CodeEditor::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu* menu = createStandardContextMenu(e->pos());

    if (m_spell && spellCheckApplies()) {
        const int pos = cursorForPosition(e->pos()).position();
        for (const auto& range : std::as_const(m_spellRanges)) {
            if (pos < range.first || pos > range.second) continue;
            QTextCursor wc(document());
            wc.setPosition(range.first);
            wc.setPosition(range.second, QTextCursor::KeepAnchor);
            const QString word = wc.selectedText();

            QAction* anchor = menu->actions().isEmpty() ? nullptr : menu->actions().first();
            const QStringList suggestions = SpellChecker::instance().suggest(word);
            for (const QString& sug : suggestions) {
                QAction* a = new QAction(sug, menu);
                QFont bold = a->font();
                bold.setBold(true);
                a->setFont(bold);
                connect(a, &QAction::triggered, this, [this, range, sug] {
                    QTextCursor c(document());
                    c.setPosition(range.first);
                    c.setPosition(range.second, QTextCursor::KeepAnchor);
                    c.insertText(sug);
                });
                menu->insertAction(anchor, a);
            }
            if (suggestions.isEmpty()) {
                QAction* none = new QAction(tr("(no suggestions)"), menu);
                none->setEnabled(false);
                menu->insertAction(anchor, none);
            }
            menu->insertSeparator(anchor);
            QAction* add = new QAction(tr("Add \"%1\" to dictionary").arg(word), menu);
            connect(add, &QAction::triggered, this, [this, word] {
                SpellChecker::instance().addToDictionary(word);
                runSpellCheck();
            });
            QAction* ignore = new QAction(tr("Ignore \"%1\"").arg(word), menu);
            connect(ignore, &QAction::triggered, this, [this, word] {
                SpellChecker::instance().ignoreForSession(word);
                runSpellCheck();
            });
            menu->insertAction(anchor, add);
            menu->insertAction(anchor, ignore);
            menu->insertSeparator(anchor);
            break;
        }
    }
    menu->exec(e->globalPos());
    delete menu;
}

// ---------------------------------------------------------------------------------------------
//  Code suggestions (word / keyword completion popup)
// ---------------------------------------------------------------------------------------------

void CodeEditor::ensureCompleter()
{
    if (m_completer) return;
    m_wordsModel = new QStringListModel(this);
    m_completer = new QCompleter(this);
    m_completer->setModel(m_wordsModel);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->setMaxVisibleItems(10);
    connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this, &CodeEditor::insertCompletion);
}

void CodeEditor::setSuggestionsEnabled(bool on)
{
    m_suggest = on;
    if (on) ensureCompleter();
    if (on) rebuildWords();
    else if (m_completer && !m_extMode) m_completer->popup()->hide();
}

void CodeEditor::rebuildWords()
{
    if (!m_suggest || !m_wordsModel || m_extMode) return;
    QSet<QString> words(m_langWords.begin(), m_langWords.end());
    static const QRegularExpression re(QStringLiteral("[A-Za-z_][A-Za-z0-9_]{2,}"));
    const QString text = toPlainText().left(400000);
    auto it = re.globalMatch(text);
    while (it.hasNext()) words.insert(it.next().captured());
    QStringList list(words.begin(), words.end());
    list.sort(Qt::CaseInsensitive);
    m_wordsModel->setStringList(list);
}

QString CodeEditor::wordPrefixAtCursor(int* start) const
{
    const QTextCursor c = textCursor();
    const QString line = c.block().text();
    int i = c.positionInBlock();
    const int end = i;
    while (i > 0 && (line.at(i - 1).isLetterOrNumber() || line.at(i - 1) == QLatin1Char('_'))) --i;
    if (start) *start = c.block().position() + i;
    return line.mid(i, end - i);
}

void CodeEditor::showCompletion(bool force)
{
    if (!m_suggest || !m_completer) return;
    if (m_wordsTimer.isActive()) { m_wordsTimer.stop(); rebuildWords(); }   // include words typed a moment ago

    int start = 0;
    const QString prefix = wordPrefixAtCursor(&start);
    if (prefix.size() < (force ? 1 : 2) || textCursor().hasSelection()) { m_completer->popup()->hide(); return; }

    if (prefix != m_completer->completionPrefix()) {
        m_completer->setCompletionPrefix(prefix);
        m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    }
    const int count = m_completer->completionCount();
    if (count == 0 || (count == 1 && m_completer->currentCompletion().compare(prefix, Qt::CaseSensitive) == 0)) {
        m_completer->popup()->hide();
        return;
    }
    QTextCursor sc = textCursor();
    sc.setPosition(start);
    QRect r = cursorRect(sc);
    r.setWidth(qMax(180, m_completer->popup()->sizeHintForColumn(0) + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 8));
    m_completer->complete(r);
}

void CodeEditor::insertCompletion(const QString& text)
{
    int start = 0;
    const QString prefix = wordPrefixAtCursor(&start);
    QTextCursor c = textCursor();
    c.setPosition(start);
    c.setPosition(start + int(prefix.size()), QTextCursor::KeepAnchor);
    if (m_extMode && m_extItems.contains(text)) {
        const LspCompletionItem item = m_extItems.value(text);
        m_extMode = false;
        c.removeSelectedText();
        setTextCursor(c);
        if (item.snippet) insertSnippet(item.insertText);
        else insertPlainText(item.insertText);
        if (m_suggest) rebuildWords();
        return;
    }
    c.insertText(text);
    setTextCursor(c);
}

// ---- Language Server: completion popup, diagnostics, hover ------------------------------------------

void CodeEditor::showCompletionItems(const QList<LspCompletionItem>& items)
{
    if (items.isEmpty() || !hasFocus()) return;
    ensureCompleter();
    m_extItems.clear();
    QStringList labels;
    for (const LspCompletionItem& it : items) {
        if (m_extItems.contains(it.label)) continue;
        m_extItems.insert(it.label, it);
        labels << it.label;
    }
    m_extMode = true;
    m_wordsModel->setStringList(labels);
    m_completer->setCompletionPrefix(QString());
    refreshExternalPopup();
}

void CodeEditor::refreshExternalPopup()
{
    if (!m_completer) return;
    int start = 0;
    const QString prefix = wordPrefixAtCursor(&start);
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() == 0) { m_completer->popup()->hide(); m_extMode = false; return; }
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    QTextCursor sc = textCursor();
    sc.setPosition(start);
    QRect r = cursorRect(sc);
    r.setWidth(qMax(260, m_completer->popup()->sizeHintForColumn(0) + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 8));
    m_completer->complete(r);
}

void CodeEditor::setLspDiagnostics(const QList<LspDiagnostic>& list)
{
    m_lspDiags = list;
    m_lspMarks.clear();
    m_lspSelections.clear();
    const ThemeColors& tc = ThemeManager::instance().colors();
    for (const LspDiagnostic& d : list) {
        const QTextBlock b1 = document()->findBlockByNumber(d.line), b2 = document()->findBlockByNumber(d.endLine);
        if (!b1.isValid()) continue;
        int from = b1.position() + qMin(d.startChar, qMax(0, int(b1.length()) - 1));
        int to = b2.isValid() ? b2.position() + qMin(d.endChar, qMax(0, int(b2.length()) - 1)) : from;
        const int lineEnd = b1.position() + qMax(0, int(b1.length()) - 1);     // end of the line's text
        if (to <= from) to = qMin(from + 1, lineEnd);                          // empty range: underline one character
        if (to <= from) from = qMax(b1.position(), from - 1);                  // at end of line: the previous one
        LspMark m;
        m.d = d;
        m.cursor = QTextCursor(document());
        m.cursor.setPosition(from);
        m.cursor.setPosition(to, QTextCursor::KeepAnchor);
        m_lspMarks.append(m);
        QTextEdit::ExtraSelection sel;
        sel.cursor = m.cursor;
        sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        sel.format.setUnderlineColor(d.severity == 1 ? tc.error : d.severity == 2 ? tc.warning : tc.textDim);
        m_lspSelections.append(sel);
    }
    rebuildExtraSelections();
}

bool CodeEditor::viewportEvent(QEvent* e)
{
    if (e->type() == QEvent::ToolTip && !m_lspMarks.isEmpty()) {
        const auto* he = static_cast<QHelpEvent*>(e);
        const int pos = cursorForPosition(he->pos()).position();
        QStringList msgs;
        for (const LspMark& m : m_lspMarks)
            if (pos >= m.cursor.selectionStart() && pos <= m.cursor.selectionEnd())
                msgs << (m.d.source.isEmpty() ? m.d.message : QStringLiteral("%1 (%2)").arg(m.d.message, m.d.source));
        if (!msgs.isEmpty()) {
            QToolTip::showText(he->globalPos(), msgs.join(QStringLiteral("\n")), viewport());
            return true;
        }
    }
    return QPlainTextEdit::viewportEvent(e);
}

void CodeEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier) && m_lspActive) {
        setTextCursor(cursorForPosition(e->pos()));
        emit lspDefinitionRequested();
        e->accept();
        return;
    }
    QPlainTextEdit::mousePressEvent(e);
}

void CodeEditor::showHoverText(const QString& text)
{
    if (text.trimmed().isEmpty()) return;
    QToolTip::showText(mapToGlobal(cursorRect().bottomLeft()), text.left(1500), this);
}

QString CodeEditor::languageName() const
{
    const QString ext = QFileInfo(m_path).suffix().toLower();
    return ext;
}

// ---------------------------------------------------------------------------------------------
//  AI ghost text
// ---------------------------------------------------------------------------------------------

void CodeEditor::setAiCompletion(AiCompletion* ai, bool on)
{
    if (m_ai != ai) {
        m_ai = ai;
        if (ai) {
            connect(ai, &AiCompletion::completionReady, this, [this](quint64 id, const QString& text) {
                if (id != m_ghostRequest || textCursor().position() != m_ghostPos) return;
                QString line = text.section(QLatin1Char('\n'), 0, 0);
                while (!line.isEmpty() && line.at(line.size() - 1).isSpace()) line.chop(1);
                if (line.trimmed().isEmpty()) return;
                m_ghost = line;
                viewport()->update();
            });
        }
    }
    m_aiOn = on && ai;
    if (!m_aiOn) { m_ghostTimer.stop(); clearGhost(); }
}

QString CodeEditor::textRange(int from, int to) const
{
    QTextCursor c(document());
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    QString t = c.selectedText();
    t.replace(QChar(0x2029), QLatin1Char('\n'));
    return t;
}

void CodeEditor::requestGhost()
{
    if (!m_aiOn || !m_ai || !m_ai->isReady() || textCursor().hasSelection()) return;
    if (m_completer && m_completer->popup()->isVisible()) return;
    const QTextCursor c = textCursor();
    if (!c.block().text().mid(c.positionInBlock()).trimmed().isEmpty()) return;   // only at the end of a line

    const int pos = c.position();
    const int total = qMax(0, document()->characterCount() - 1);
    m_ghostPos = pos;
    m_ghostRequest = m_ai->requestCompletion(textRange(qMax(0, pos - 3000), pos), textRange(pos, qMin(total, pos + 800)));
}

void CodeEditor::clearGhost()
{
    if (m_ghost.isEmpty()) return;
    m_ghost.clear();
    viewport()->update();
}

void CodeEditor::acceptGhost()
{
    const QString g = m_ghost;
    m_ghost.clear();
    insertPlainText(g);
    m_ghostTimer.start(450);   // keep going: suggest the next piece
}

void CodeEditor::paintEvent(QPaintEvent* e)
{
    QPlainTextEdit::paintEvent(e);
    const ThemeColors& tc = ThemeManager::instance().colors();
    constexpr int kCaretWidth = 2;

    if (hasFocus() && m_caretOn && !isReadOnly()) {
        QPainter p(viewport());
        const QRect cr = cursorRect();
        p.fillRect(cr.x(), cr.y(), kCaretWidth, cr.height(), tc.text);
    }

    if (m_ghost.isEmpty() || textCursor().position() != m_ghostPos) return;
    QPainter p(viewport());
    QColor c = tc.textDim;
    c.setAlpha(210);
    p.setPen(c);
    QFont f = font();
    f.setItalic(true);
    p.setFont(f);
    const QRect cr = cursorRect();
    p.drawText(cr.x() + kCaretWidth, cr.top() + QFontMetrics(f).ascent(), m_ghost);
}


// ---------------------------------------------------------------------------------------------
//  Find: highlight all matches
// ---------------------------------------------------------------------------------------------

void CodeEditor::setSearchHighlight(const QString& text, bool caseSensitive, bool wholeWord, bool regex)
{
    m_searchText = text;
    m_searchCase = caseSensitive;
    m_searchWhole = wholeWord;
    m_searchRegex = regex;
    rerunSearch();
}

void CodeEditor::clearSearchHighlight()
{
    m_searchText.clear();
    m_searchTimer.stop();
    m_searchSelections.clear();
    m_searchRanges.clear();
    rebuildExtraSelections();
}

void CodeEditor::rerunSearch()
{
    m_searchSelections.clear();
    m_searchRanges.clear();
    if (m_searchText.isEmpty()) { rebuildExtraSelections(); return; }

    QString pattern = m_searchRegex ? m_searchText : QRegularExpression::escape(m_searchText);
    if (m_searchWhole) pattern = QStringLiteral("\\b(?:") + pattern + QStringLiteral(")\\b");
    const QRegularExpression re(pattern, m_searchCase ? QRegularExpression::NoPatternOption
                                                      : QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid()) { rebuildExtraSelections(); return; }

    const ThemeColors& tc = ThemeManager::instance().colors();
    constexpr int kMaxMatches = 20000;
    for (QTextBlock b = document()->begin(); b.isValid() && m_searchRanges.size() < kMaxMatches; b = b.next()) {
        const QString text = b.text();
        auto it = re.globalMatch(text);
        while (it.hasNext() && m_searchRanges.size() < kMaxMatches) {
            const auto m = it.next();
            if (m.capturedLength() == 0) continue;
            const int s = b.position() + int(m.capturedStart());
            const int e = s + int(m.capturedLength());
            m_searchRanges.append(qMakePair(s, e));
            QTextEdit::ExtraSelection sel;
            sel.cursor = QTextCursor(document());
            sel.cursor.setPosition(s);
            sel.cursor.setPosition(e, QTextCursor::KeepAnchor);
            sel.format.setBackground(tc.findMatch);
            m_searchSelections.append(sel);
        }
    }
    rebuildExtraSelections();
}

int CodeEditor::searchIndexAt(int position) const
{
    for (int i = 0; i < m_searchRanges.size(); ++i)
        if (m_searchRanges.at(i).first == position) return i + 1;
    return 0;
}

void CodeEditor::setLineMarks(const QHash<int, int>& marks)
{
    if (marks == m_lineMarks) return;
    m_lineMarks = marks;
    m_gutter->update();
}
