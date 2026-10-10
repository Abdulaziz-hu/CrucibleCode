#include "CodeEditor.h"
#include "SyntaxHighlighter.h"
#include "ThemeManager.h"

#include <climits>

#include <QFile>
#include <QFontMetricsF>
#include <QPaintEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
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

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("codeEditor"));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameStyle(QFrame::NoFrame);
    setCursorWidth(2);

    m_gutter = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        const QTextCursor c = textCursor();
        emit cursorInfoChanged(c.blockNumber() + 1, c.positionInBlock() + 1);
    });

    updateLineNumberAreaWidth();
    applyTheme();
}

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

bool CodeEditor::isMarkdown() const
{
    const QString ext = QFileInfo(m_path).suffix().toLower();
    return ext == QLatin1String("md") || ext == QLatin1String("markdown") || ext == QLatin1String("mdown")
        || ext == QLatin1String("mkd") || ext == QLatin1String("mdx");
}

void CodeEditor::setEditorFont(const QFont& f)
{
    setFont(f);
    setTabStopDistance(m_tabSize * QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')));
    updateLineNumberAreaWidth();
    m_gutter->update();
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
    QList<QTextEdit::ExtraSelection> extras;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(tc.lineHighlight);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        extras.append(sel);
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
            extras.append(s);
        };
        mark(at, match >= 0);
        if (match >= 0) mark(match, true);
    }
    setExtraSelections(extras);
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

void CodeEditor::keyPressEvent(QKeyEvent* e)
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
