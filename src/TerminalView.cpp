#include "TerminalView.h"
#include "PtyProcess.h"
#include "TerminalScreen.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

namespace {

#ifdef Q_OS_MACOS
constexpr bool kMac = true;
#else
constexpr bool kMac = false;
#endif

// 16 ANSI colours per theme
const char* const kDarkAnsi[16] = {
    "#000000", "#CD3131", "#0DBC79", "#E5E510", "#2472C8", "#BC3FBC", "#11A8CD", "#E5E5E5",
    "#666666", "#F14C4C", "#23D18B", "#F5F543", "#3B8EEA", "#D670D6", "#29B8DB", "#FFFFFF"};
const char* const kLightAnsi[16] = {
    "#000000", "#CD3131", "#00BC00", "#949800", "#0451A5", "#BC05BC", "#0598BC", "#555555",
    "#666666", "#CD3131", "#14CE14", "#B5BA00", "#0451A5", "#BC05BC", "#0598BC", "#A5A5A5"};

} // namespace

TerminalView::TerminalView(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    setCursor(Qt::IBeamCursor);

    m_screen = new TerminalScreen(m_cols, m_rows, this);
    m_pty = new PtyProcess(this);
    m_bar = new QScrollBar(Qt::Vertical, this);

    connect(m_pty, &PtyProcess::dataReceived, this, &TerminalView::onData);
    connect(m_pty, &PtyProcess::exited, this, [this](int code) {
        m_exited = true;
        emit finished(code);
    });
    connect(m_screen, &TerminalScreen::reply, this, [this](const QByteArray& b) { m_pty->write(b); });
    connect(m_screen, &TerminalScreen::titleChanged, this, [this](const QString& t) {
        m_title = t;
        emit titleChanged(t);
    });
    connect(m_bar, &QScrollBar::valueChanged, this, [this](int v) {
        m_offset = m_bar->maximum() - v;
        update();
    });

    m_repaint.setSingleShot(true);
    m_repaint.setInterval(8);
    connect(&m_repaint, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    m_blink.setInterval(530);
    connect(&m_blink, &QTimer::timeout, this, [this] {
        m_cursorOn = !m_cursorOn;
        update();
    });

    buildFonts(font());
}

TerminalView::~TerminalView() = default;

void TerminalView::buildFonts(const QFont& f)
{
    m_font = f;
    m_font.setStyleHint(QFont::Monospace);
    m_font.setFixedPitch(true);
    m_fontBold = m_font;
    m_fontBold.setBold(true);
    m_fontItalic = m_font;
    m_fontItalic.setItalic(true);
    m_fontBoldItalic = m_fontBold;
    m_fontBoldItalic.setItalic(true);
    const QFontMetrics fm(m_font);
    m_cw = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
    m_ch = qMax(1, fm.height());
    m_ascent = fm.ascent();
}

void TerminalView::setTerminalFont(const QFont& font)
{
    buildFonts(font);
    recomputeGrid();
    update();
}

void TerminalView::applyTheme() { update(); }

void TerminalView::setShell(const QString& program, const QStringList& args, const QString& workingDir)
{
    m_program = program;
    m_args = args;
    m_cwd = workingDir;
    m_pendingStart = true;
    maybeStart();
}

void TerminalView::maybeStart()
{
    if (!m_pendingStart || !isVisible() || width() < 60 || height() < 30) return;
    m_pendingStart = false;
    recomputeGrid();
    QString err;
    if (!m_pty->start(m_program, m_args, m_cwd, m_cols, m_rows, &err)) {
        m_screen->write(QStringLiteral("\x1b[31mFailed to start shell: %1\x1b[0m\r\n").arg(err).toUtf8());
        m_exited = true;
        update();
    }
}

void TerminalView::recomputeGrid()
{
    const int barW = m_bar->isVisible() ? m_bar->width() : m_bar->sizeHint().width();
    const int cols = qMax(2, (width() - barW) / m_cw);
    const int rows = qMax(1, height() / m_ch);
    if (cols == m_cols && rows == m_rows) return;
    m_cols = cols;
    m_rows = rows;
    m_screen->resize(cols, rows);
    if (m_pty->isRunning()) m_pty->resize(cols, rows);
    m_offset = qMin(m_offset, m_screen->scrollbackSize());
    updateScrollBar();
}

void TerminalView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    const int w = m_bar->sizeHint().width();
    m_bar->setGeometry(width() - w, 0, w, height());
    recomputeGrid();
    maybeStart();
}

void TerminalView::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    QTimer::singleShot(0, this, [this] { recomputeGrid(); maybeStart(); });
}

void TerminalView::updateScrollBar()
{
    const int sb = m_screen->scrollbackSize();
    const QSignalBlocker blocker(m_bar);
    m_bar->setRange(0, sb);
    m_bar->setPageStep(m_rows);
    m_bar->setValue(sb - m_offset);
}

void TerminalView::onData(const QByteArray& data)
{
    const int before = m_screen->scrollbackSize();
    m_screen->write(data);
    const int after = m_screen->scrollbackSize();
    if (m_offset > 0) m_offset = qMin(m_offset + (after - before), after);
    updateScrollBar();
    if (!m_repaint.isActive()) m_repaint.start();
}

// ---- painting ------------------------------------------------------------------------------

QColor TerminalView::resolve(quint32 code, bool isFg) const
{
    const ThemeColors& tc = ThemeManager::instance().colors();
    const quint32 kind = code >> 24;
    const quint32 v = code & 0xFFFFFF;
    if (kind == 0) return isFg ? tc.text : tc.base;
    if (kind == 2) return QColor((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);

    const int i = int(v & 0xFF);
    if (i < 16) return QColor(QLatin1String(ThemeManager::instance().isDark() ? kDarkAnsi[i] : kLightAnsi[i]));
    if (i < 232) {
        const int n = i - 16;
        const auto comp = [](int c) { return c ? 55 + 40 * c : 0; };
        return QColor(comp(n / 36), comp((n / 6) % 6), comp(n % 6));
    }
    const int g = 8 + 10 * (i - 232);
    return QColor(g, g, g);
}

bool TerminalView::inSelection(int line, int col) const
{
    if (!m_hasSel) return false;
    CellPos a = m_selA, b = m_selB;
    if (a.line > b.line || (a.line == b.line && a.col > b.col)) std::swap(a, b);
    if (line < a.line || line > b.line) return false;
    if (line == a.line && col < a.col) return false;
    if (line == b.line && col > b.col) return false;
    return true;
}

void TerminalView::drawLine(QPainter& p, int row, int lineIndex)
{
    const ThemeColors& tc = ThemeManager::instance().colors();
    const TermLine& line = m_screen->lineAt(lineIndex);
    const int y = row * m_ch;

    int runStart = 0;
    QString run;
    QColor runFg, runBg;
    quint8 runAttrs = 0;
    bool haveRun = false;

    const auto flush = [&](int endCol) {
        if (!haveRun) return;
        const int x = runStart * m_cw;
        const int w = (endCol - runStart) * m_cw;
        if (runBg != tc.base) p.fillRect(x, y, w, m_ch, runBg);
        const bool bold = runAttrs & AttrBold, italic = runAttrs & AttrItalic;
        p.setFont(bold ? (italic ? m_fontBoldItalic : m_fontBold) : (italic ? m_fontItalic : m_font));
        p.setPen(runFg);
        p.drawText(x, y + m_ascent, run);
        if (runAttrs & AttrUnderline) p.drawLine(x, y + m_ch - 2, x + w, y + m_ch - 2);
        if (runAttrs & AttrStrike) p.drawLine(x, y + m_ch / 2, x + w, y + m_ch / 2);
        run.clear();
        haveRun = false;
    };

    for (int c = 0; c < m_cols; ++c) {
        TermCell cell;
        if (c < line.size()) cell = line[c];

        QColor fg = resolve(cell.fg, true), bg = resolve(cell.bg, false);
        if (cell.attrs & AttrInverse) std::swap(fg, bg);
        if (cell.attrs & AttrDim) fg = QColor::fromRgbF(
            (fg.redF() + bg.redF()) / 2, (fg.greenF() + bg.greenF()) / 2, (fg.blueF() + bg.blueF()) / 2);
        if (inSelection(lineIndex, c)) { bg = tc.selection; fg = tc.text; }

        if (haveRun && (fg != runFg || bg != runBg || cell.attrs != runAttrs)) flush(c);
        if (!haveRun) {
            haveRun = true;
            runStart = c;
            runFg = fg;
            runBg = bg;
            runAttrs = cell.attrs;
        }
        if (cell.ch < 0x10000) run += QChar(char16_t(cell.ch));
        else run += QString::fromUcs4(&cell.ch, 1);
    }
    flush(m_cols);
}

void TerminalView::paintEvent(QPaintEvent*)
{
    const ThemeColors& tc = ThemeManager::instance().colors();
    QPainter p(this);
    p.fillRect(rect(), tc.base);
    p.setFont(m_font);

    const int topLine = m_screen->scrollbackSize() - m_offset;
    for (int r = 0; r < m_rows; ++r) drawLine(p, r, topLine + r);

    if (m_offset == 0 && m_screen->cursorVisible() && !m_exited) {
        const int cx = m_screen->cursorX(), cy = m_screen->cursorY();
        const QRect cr(cx * m_cw, cy * m_ch, m_cw, m_ch);
        if (hasFocus()) {
            if (m_cursorOn) {
                p.fillRect(cr, tc.text);
                const TermLine& line = m_screen->lineAt(topLine + cy);
                if (cx < line.size() && line[cx].ch != U' ') {
                    p.setPen(tc.base);
                    p.setFont(m_font);
                    p.drawText(cr.x(), cr.y() + m_ascent, QString::fromUcs4(&line[cx].ch, 1));
                }
            }
        } else {
            p.setPen(tc.text);
            p.drawRect(cr.adjusted(0, 0, -1, -1));
        }
    }
}

// ---- focus / events ----------------------------------------------------------------------------

void TerminalView::focusInEvent(QFocusEvent* e)
{
    QWidget::focusInEvent(e);
    m_cursorOn = true;
    m_blink.start();
    update();
}

void TerminalView::focusOutEvent(QFocusEvent* e)
{
    QWidget::focusOutEvent(e);
    m_blink.stop();
    update();
}

bool TerminalView::focusNextPrevChild(bool) { return false; }

bool TerminalView::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto* k = static_cast<QKeyEvent*>(e);
        const Qt::KeyboardModifiers m = k->modifiers();
        // Let the IDE keep its "toggle terminal" shortcut (Ctrl+`).
        if ((m & Qt::ControlModifier) && k->key() == Qt::Key_QuoteLeft) return QWidget::event(e);
        if (kMac && (m & Qt::ControlModifier)) {          // Cmd on macOS: only copy / paste are ours
            if (k->key() == Qt::Key_C || k->key() == Qt::Key_V) { e->accept(); return true; }
            return QWidget::event(e);
        }
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

// ---- keyboard ---------------------------------------------------------------------------------------

QByteArray TerminalView::encodeKey(QKeyEvent* e) const
{
    const Qt::KeyboardModifiers m = e->modifiers();
    const bool shift = m & Qt::ShiftModifier;
    const bool alt = m & Qt::AltModifier;
    const bool ctrl = kMac ? (m & Qt::MetaModifier) : (m & Qt::ControlModifier);
    const int key = e->key();

    const auto modParam = [&] { return 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0); };
    const auto cursorKey = [&](char fin) -> QByteArray {
        if (modParam() > 1) return "\x1b[1;" + QByteArray::number(modParam()) + fin;
        return m_screen->appCursorKeys() ? QByteArray("\x1bO") + fin : QByteArray("\x1b[") + fin;
    };
    const auto tilde = [&](int n) -> QByteArray {
        if (modParam() > 1) return "\x1b[" + QByteArray::number(n) + ";" + QByteArray::number(modParam()) + "~";
        return "\x1b[" + QByteArray::number(n) + "~";
    };

    switch (key) {
    case Qt::Key_Up: return cursorKey('A');
    case Qt::Key_Down: return cursorKey('B');
    case Qt::Key_Right: return cursorKey('C');
    case Qt::Key_Left: return cursorKey('D');
    case Qt::Key_Home: return cursorKey('H');
    case Qt::Key_End: return cursorKey('F');
    case Qt::Key_PageUp: return tilde(5);
    case Qt::Key_PageDown: return tilde(6);
    case Qt::Key_Insert: return tilde(2);
    case Qt::Key_Delete: return tilde(3);
    case Qt::Key_Backspace: return ctrl ? QByteArray("\x08") : QByteArray("\x7f");
    case Qt::Key_Tab: return "\t";
    case Qt::Key_Backtab: return "\x1b[Z";
    case Qt::Key_Return: case Qt::Key_Enter: return "\r";
    case Qt::Key_Escape: return "\x1b";
    case Qt::Key_F1: return "\x1bOP";
    case Qt::Key_F2: return "\x1bOQ";
    case Qt::Key_F3: return "\x1bOR";
    case Qt::Key_F4: return "\x1bOS";
    case Qt::Key_F5: return tilde(15);
    case Qt::Key_F6: return tilde(17);
    case Qt::Key_F7: return tilde(18);
    case Qt::Key_F8: return tilde(19);
    case Qt::Key_F9: return tilde(20);
    case Qt::Key_F10: return tilde(21);
    case Qt::Key_F11: return tilde(23);
    case Qt::Key_F12: return tilde(24);
    default: break;
    }

    if (ctrl) {
        if (key >= Qt::Key_A && key <= Qt::Key_Z) return QByteArray(1, char(key - Qt::Key_A + 1));
        switch (key) {
        case Qt::Key_Space: case Qt::Key_At: return QByteArray(1, '\0');
        case Qt::Key_BracketLeft: return "\x1b";
        case Qt::Key_Backslash: return "\x1c";
        case Qt::Key_BracketRight: return "\x1d";
        case Qt::Key_AsciiCircum: case Qt::Key_6: return "\x1e";
        case Qt::Key_Underscore: case Qt::Key_Minus: return "\x1f";
        default: break;
        }
    }

    const QString text = e->text();
    if (text.isEmpty() || text.at(0).unicode() < 0x20) return {};
    QByteArray out = text.toUtf8();
    if (alt) out.prepend('\x1b');
    return out;
}

void TerminalView::sendText(const QByteArray& bytes)
{
    if (!bytes.isEmpty() && m_pty->isRunning()) m_pty->write(bytes);
}

void TerminalView::keyPressEvent(QKeyEvent* e)
{
    const Qt::KeyboardModifiers m = e->modifiers();
    const int key = e->key();
    const bool ctrlShift = (m & Qt::ControlModifier) && (m & Qt::ShiftModifier);

    // --- clipboard / view shortcuts ---
    if (kMac) {
        if ((m & Qt::ControlModifier) && key == Qt::Key_C) { copySelection(); return; }   // Cmd+C
        if ((m & Qt::ControlModifier) && key == Qt::Key_V) { pasteClipboard(); return; }  // Cmd+V
    } else {
        if (ctrlShift && key == Qt::Key_C) { copySelection(); return; }
        if (ctrlShift && key == Qt::Key_V) { pasteClipboard(); return; }
        if ((m & Qt::ControlModifier) && key == Qt::Key_Insert) { copySelection(); return; }
        if ((m & Qt::ShiftModifier) && key == Qt::Key_Insert) { pasteClipboard(); return; }
#ifdef Q_OS_WIN
        if ((m & Qt::ControlModifier) && !(m & Qt::ShiftModifier) && key == Qt::Key_V) { pasteClipboard(); return; }
#endif
        if ((m & Qt::ControlModifier) && !(m & Qt::ShiftModifier) && key == Qt::Key_C && m_hasSel) {
            copySelection();   // like Windows Terminal: copy when text is selected, else send SIGINT
            m_hasSel = false;
            update();
            return;
        }
    }
    if ((m & Qt::ShiftModifier) && key == Qt::Key_PageUp) { m_bar->setValue(m_bar->value() - m_rows / 2); return; }
    if ((m & Qt::ShiftModifier) && key == Qt::Key_PageDown) { m_bar->setValue(m_bar->value() + m_rows / 2); return; }

    const QByteArray bytes = encodeKey(e);
    if (bytes.isEmpty()) { e->ignore(); return; }

    if (m_offset != 0) { m_offset = 0; updateScrollBar(); }
    if (m_hasSel) { m_hasSel = false; }
    m_cursorOn = true;
    sendText(bytes);
    update();
}

// ---- selection / clipboard ---------------------------------------------------------------------------------

TerminalView::CellPos TerminalView::cellAt(const QPoint& pt) const
{
    CellPos c;
    const int row = qBound(0, pt.y() / m_ch, m_rows - 1);
    c.col = qBound(0, pt.x() / m_cw, m_cols - 1);
    c.line = m_screen->scrollbackSize() - m_offset + row;
    return c;
}

QString TerminalView::selectedText() const
{
    if (!m_hasSel) return {};
    CellPos a = m_selA, b = m_selB;
    if (a.line > b.line || (a.line == b.line && a.col > b.col)) std::swap(a, b);
    QStringList lines;
    for (int l = a.line; l <= b.line; ++l) {
        const TermLine& line = m_screen->lineAt(l);
        const int from = (l == a.line) ? a.col : 0;
        const int to = (l == b.line) ? b.col : int(line.size()) - 1;
        QString s;
        for (int c = from; c <= to && c < line.size(); ++c) s += QString::fromUcs4(&line[c].ch, 1);
        while (s.endsWith(QLatin1Char(' '))) s.chop(1);
        lines << s;
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalView::copySelection()
{
    const QString t = selectedText();
    if (!t.isEmpty()) QApplication::clipboard()->setText(t);
}

void TerminalView::pasteClipboard()
{
    QString t = QApplication::clipboard()->text();
    if (t.isEmpty()) return;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    QByteArray bytes = t.toUtf8();
    if (m_screen->bracketedPaste()) bytes = "\x1b[200~" + bytes + "\x1b[201~";
    if (m_offset != 0) { m_offset = 0; updateScrollBar(); }
    sendText(bytes);
}

void TerminalView::selectAll()
{
    m_selA = {0, 0};
    const int last = m_screen->scrollbackSize() + m_rows - 1;
    m_selB = {last, m_cols - 1};
    m_hasSel = true;
    update();
}

void TerminalView::clearScreen()
{
    m_screen->clearScrollback();
    m_screen->write("\x1b[2J\x1b[H");
    m_offset = 0;
    m_hasSel = false;
    updateScrollBar();
    sendText("\x0c");   // ask the shell to redraw its prompt
    update();
}

void TerminalView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    if (e->button() == Qt::LeftButton) {
        m_selecting = true;
        m_hasSel = false;
        m_selA = m_selB = cellAt(e->position().toPoint());
        update();
    } else if (e->button() == Qt::MiddleButton && QApplication::clipboard()->supportsSelection()) {
        const QString t = QApplication::clipboard()->text(QClipboard::Selection);
        if (!t.isEmpty()) sendText(t.toUtf8());
    }
}

void TerminalView::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_selecting) return;
    const CellPos c = cellAt(e->position().toPoint());
    if (c.line != m_selB.line || c.col != m_selB.col) {
        m_selB = c;
        m_hasSel = (m_selA.line != m_selB.line || m_selA.col != m_selB.col);
        update();
    }
}

void TerminalView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    m_selecting = false;
    if (m_hasSel && QApplication::clipboard()->supportsSelection())
        QApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const CellPos c = cellAt(e->position().toPoint());
    const TermLine& line = m_screen->lineAt(c.line);
    const auto isWord = [&](int col) {
        if (col < 0 || col >= line.size()) return false;
        const char32_t ch = line[col].ch;
        return ch != U' ' && ch != U'\t';
    };
    if (!isWord(c.col)) return;
    int a = c.col, b = c.col;
    while (isWord(a - 1)) --a;
    while (isWord(b + 1)) ++b;
    m_selA = {c.line, a};
    m_selB = {c.line, b};
    m_hasSel = true;
    update();
}

void TerminalView::wheelEvent(QWheelEvent* e)
{
    const int steps = e->angleDelta().y() / 40;   // 3 lines per notch
    if (steps != 0) m_bar->setValue(m_bar->value() - steps);
    e->accept();
}

void TerminalView::contextMenuEvent(QContextMenuEvent* e)
{
    QMenu menu(this);
    QAction* copy = menu.addAction(tr("Copy"));
    copy->setEnabled(m_hasSel);
    QAction* paste = menu.addAction(tr("Paste"));
    QAction* all = menu.addAction(tr("Select All"));
    menu.addSeparator();
    QAction* clear = menu.addAction(tr("Clear"));
    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == copy) copySelection();
    else if (chosen == paste) pasteClipboard();
    else if (chosen == all) selectAll();
    else if (chosen == clear) clearScreen();
}
