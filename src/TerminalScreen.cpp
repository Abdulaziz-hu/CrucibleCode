#include "TerminalScreen.h"

#include <algorithm>

TerminalScreen::TerminalScreen(int cols, int rows, QObject* parent)
    : QObject(parent), m_cols(qMax(2, cols)), m_rows(qMax(1, rows))
{
    m_grid = blankGrid();
    m_altGrid = blankGrid();
    m_top = 0;
    m_bottom = m_rows - 1;
}

TermCell TerminalScreen::blankCell() const
{
    TermCell c;
    c.bg = m_bg;
    return c;
}

TermLine TerminalScreen::blankLine() const { return TermLine(m_cols, blankCell()); }

QVector<TermLine> TerminalScreen::blankGrid() const
{
    return QVector<TermLine>(m_rows, blankLine());
}

const TermLine& TerminalScreen::lineAt(int index) const
{
    static const TermLine empty;
    const int sb = scrollbackSize();
    if (index < 0) return empty;
    if (index < sb) return m_scrollback[size_t(index)];
    const int r = index - sb;
    return r < m_grid.size() ? m_grid[r] : empty;
}

void TerminalScreen::clampCursor()
{
    m_cx = qBound(0, m_cx, m_cols - 1);
    m_cy = qBound(0, m_cy, m_rows - 1);
}

// ---- input -------------------------------------------------------------------------

void TerminalScreen::write(const QByteArray& data)
{
    const QString text = m_decoder(data);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char16_t u = text.at(i).unicode();
        char32_t cp = u;
        if (QChar::isHighSurrogate(u) && i + 1 < text.size() && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            cp = QChar::surrogateToUcs4(u, text.at(i + 1).unicode());
            ++i;
        }
        process(cp);
    }
}

void TerminalScreen::process(char32_t c)
{
    switch (m_state) {
    case Ground:
        if (c == 0x1B) m_state = Esc;
        else if (c < 0x20) control(c);
        else if (c == 0x7F) { /* DEL: ignored */ }
        else putChar(c);
        break;

    case Esc:
        m_state = Ground;
        switch (c) {
        case '[': m_state = Csi; m_csi.clear(); break;
        case ']': m_state = Osc; m_str.clear(); m_strIsOsc = true; break;
        case 'P': case '^': case '_': case 'X': m_state = Osc; m_str.clear(); m_strIsOsc = false; break;
        case '(': case ')': case '*': case '+': m_state = Charset; break;
        case '7': saveCursor(); break;
        case '8': restoreCursor(); break;
        case 'M': reverseIndex(); break;
        case 'D': lineFeed(); break;
        case 'E': m_cx = 0; lineFeed(); break;
        case 'c': fullReset(); break;
        default: break;   // '=', '>', ... ignored
        }
        break;

    case Charset:
        m_state = Ground;
        break;

    case Csi:
        if (c >= 0x20 && c <= 0x3F) {
            if (m_csi.size() < 256) m_csi += QChar(char16_t(c));
        } else if (c >= 0x40 && c <= 0x7E) {
            executeCsi(c);
            m_state = Ground;
        } else if (c == 0x1B) {
            m_state = Esc;
        } else if (c < 0x20) {
            control(c);
        } else {
            m_state = Ground;
        }
        break;

    case Osc:
        if (c == 0x07) { finishString(); m_state = Ground; }
        else if (c == 0x1B) m_state = OscEsc;
        else if (m_str.size() < 4096 && c < 0x10000) m_str += QChar(char16_t(c));
        break;

    case OscEsc:
        finishString();
        m_state = (c == '\\') ? Ground : Esc;
        if (m_state == Esc) process(c);   // ESC started a new sequence instead of ST
        break;
    }
}

void TerminalScreen::finishString()
{
    if (!m_strIsOsc) return;
    const int semi = int(m_str.indexOf(QLatin1Char(';')));
    if (semi < 0) return;
    const int code = m_str.left(semi).toInt();
    if (code == 0 || code == 2) emit titleChanged(m_str.mid(semi + 1));
}

void TerminalScreen::control(char32_t c)
{
    switch (c) {
    case '\n': case '\v': case '\f': lineFeed(); break;
    case '\r': m_cx = 0; m_wrapPending = false; break;
    case '\b': if (m_cx > 0) --m_cx; m_wrapPending = false; break;
    case '\t': m_cx = qMin(m_cols - 1, (m_cx / 8 + 1) * 8); m_wrapPending = false; break;
    default: break;   // BEL and the rest are ignored
    }
}

void TerminalScreen::putChar(char32_t c)
{
    const QChar::Category cat = QChar::category(c);
    if (cat == QChar::Mark_NonSpacing || cat == QChar::Other_Format) return;   // zero width

    if (m_wrapPending && m_autowrap) {
        m_cx = 0;
        lineFeed();
    }
    m_wrapPending = false;

    TermCell& cell = m_grid[m_cy][m_cx];
    cell.ch = c;
    cell.fg = m_fg;
    cell.bg = m_bg;
    cell.attrs = m_attrs;

    if (m_cx >= m_cols - 1) m_wrapPending = true;
    else ++m_cx;
}

// ---- scrolling ---------------------------------------------------------------------

void TerminalScreen::lineFeed()
{
    if (m_cy == m_bottom) scrollUp(1);
    else if (m_cy < m_rows - 1) ++m_cy;
}

void TerminalScreen::reverseIndex()
{
    if (m_cy == m_top) scrollDown(1);
    else if (m_cy > 0) --m_cy;
}

void TerminalScreen::scrollUp(int n)
{
    n = qMin(n, m_bottom - m_top + 1);
    for (int i = 0; i < n; ++i) {
        if (!m_alt && m_top == 0) {
            m_scrollback.push_back(m_grid[0]);
            if (int(m_scrollback.size()) > kMaxScrollback) m_scrollback.pop_front();
        }
        m_grid.remove(m_top);
        m_grid.insert(m_bottom, blankLine());
    }
}

void TerminalScreen::scrollDown(int n)
{
    n = qMin(n, m_bottom - m_top + 1);
    for (int i = 0; i < n; ++i) {
        m_grid.remove(m_bottom);
        m_grid.insert(m_top, blankLine());
    }
}

// ---- erasing -------------------------------------------------------------------------

void TerminalScreen::eraseLine(int mode)
{
    TermLine& l = m_grid[m_cy];
    const TermCell blank = blankCell();
    int from = 0, to = m_cols;
    if (mode == 0) from = m_cx;
    else if (mode == 1) to = m_cx + 1;
    for (int x = from; x < to && x < l.size(); ++x) l[x] = blank;
}

void TerminalScreen::eraseDisplay(int mode)
{
    if (mode == 0) {
        eraseLine(0);
        for (int y = m_cy + 1; y < m_rows; ++y) m_grid[y] = blankLine();
    } else if (mode == 1) {
        eraseLine(1);
        for (int y = 0; y < m_cy; ++y) m_grid[y] = blankLine();
    } else {
        for (int y = 0; y < m_rows; ++y) m_grid[y] = blankLine();
        if (mode == 3) m_scrollback.clear();
    }
}

void TerminalScreen::clearScrollback() { m_scrollback.clear(); }

// ---- cursor save / alternate screen --------------------------------------------------------

void TerminalScreen::saveCursor()
{
    m_savedX = m_cx; m_savedY = m_cy;
    m_savedFg = m_fg; m_savedBg = m_bg; m_savedAttrs = m_attrs;
}

void TerminalScreen::restoreCursor()
{
    m_cx = m_savedX; m_cy = m_savedY;
    m_fg = m_savedFg; m_bg = m_savedBg; m_attrs = m_savedAttrs;
    clampCursor();
    m_wrapPending = false;
}

void TerminalScreen::setAlt(bool on, bool save)
{
    if (on == m_alt) return;
    if (on) {
        if (save) saveCursor();
        std::swap(m_grid, m_altGrid);
        m_alt = true;
        m_bg = 0;
        m_grid = blankGrid();
    } else {
        std::swap(m_grid, m_altGrid);
        m_alt = false;
        if (save) restoreCursor();
    }
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrapPending = false;
}

void TerminalScreen::fullReset()
{
    m_alt = false;
    m_fg = m_bg = 0;
    m_attrs = 0;
    m_cx = m_cy = 0;
    m_top = 0;
    m_bottom = m_rows - 1;
    m_autowrap = true;
    m_cursorVisible = true;
    m_appCursor = false;
    m_bracketedPaste = false;
    m_wrapPending = false;
    m_grid = blankGrid();
    m_altGrid = blankGrid();
    m_scrollback.clear();
}

// ---- resize ----------------------------------------------------------------------------------

void TerminalScreen::resize(int cols, int rows)
{
    cols = qMax(2, cols);
    rows = qMax(1, rows);
    if (cols == m_cols && rows == m_rows) return;

    const TermCell blank = blankCell();
    for (TermLine& l : m_grid) l.resize(cols, blank);

    if (rows < m_rows) {
        int extra = m_rows - rows;
        while (extra-- > 0) {
            if (!m_alt && m_cy > 0) {
                m_scrollback.push_back(m_grid.first());
                if (int(m_scrollback.size()) > kMaxScrollback) m_scrollback.pop_front();
                m_grid.removeFirst();
                --m_cy;
            } else {
                m_grid.removeLast();
            }
        }
    } else {
        for (int i = m_rows; i < rows; ++i) m_grid.append(TermLine(cols, blank));
    }

    m_cols = cols;
    m_rows = rows;
    m_altGrid = blankGrid();
    if (m_alt) {
        // the primary screen is parked in m_altGrid while the alternate one is shown
        m_altGrid = blankGrid();
    }
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrapPending = false;
    clampCursor();
    m_savedX = qBound(0, m_savedX, m_cols - 1);
    m_savedY = qBound(0, m_savedY, m_rows - 1);
}

// ---- CSI ---------------------------------------------------------------------------------------

void TerminalScreen::executeCsi(char32_t fin)
{
    QString s = m_csi;
    QChar prefix;
    if (!s.isEmpty() && (s[0] == QLatin1Char('?') || s[0] == QLatin1Char('>')
                         || s[0] == QLatin1Char('<') || s[0] == QLatin1Char('='))) {
        prefix = s[0];
        s.remove(0, 1);
    }
    bool hasIntermediate = false;
    while (!s.isEmpty() && s.back().unicode() >= 0x20 && s.back().unicode() <= 0x2F) {
        s.chop(1);
        hasIntermediate = true;
    }
    if (hasIntermediate) return;   // cursor style, soft reset, ... ignored

    const QStringList raw = s.split(QLatin1Char(';'));
    QVector<int> p;
    for (const QString& part : raw) p.append(part.toInt());
    const auto arg = [&](int i, int def) { return (i < p.size() && p[i] > 0) ? p[i] : def; };
    const auto raw0 = [&](int i) { return i < p.size() ? p[i] : 0; };

    switch (fin) {
    case 'A': m_cy = qMax(0, m_cy - arg(0, 1)); m_wrapPending = false; break;
    case 'B': case 'e': m_cy = qMin(m_rows - 1, m_cy + arg(0, 1)); m_wrapPending = false; break;
    case 'C': case 'a': m_cx = qMin(m_cols - 1, m_cx + arg(0, 1)); m_wrapPending = false; break;
    case 'D': m_cx = qMax(0, m_cx - arg(0, 1)); m_wrapPending = false; break;
    case 'E': m_cx = 0; m_cy = qMin(m_rows - 1, m_cy + arg(0, 1)); m_wrapPending = false; break;
    case 'F': m_cx = 0; m_cy = qMax(0, m_cy - arg(0, 1)); m_wrapPending = false; break;
    case 'G': case '`': m_cx = arg(0, 1) - 1; clampCursor(); m_wrapPending = false; break;
    case 'd': m_cy = arg(0, 1) - 1; clampCursor(); m_wrapPending = false; break;
    case 'H': case 'f': m_cy = arg(0, 1) - 1; m_cx = arg(1, 1) - 1; clampCursor(); m_wrapPending = false; break;
    case 'J': eraseDisplay(raw0(0)); break;
    case 'K': eraseLine(raw0(0)); break;
    case 'L':
        if (m_cy >= m_top && m_cy <= m_bottom) {
            const int n = qMin(arg(0, 1), m_bottom - m_cy + 1);
            for (int i = 0; i < n; ++i) { m_grid.remove(m_bottom); m_grid.insert(m_cy, blankLine()); }
        }
        break;
    case 'M':
        if (m_cy >= m_top && m_cy <= m_bottom) {
            const int n = qMin(arg(0, 1), m_bottom - m_cy + 1);
            for (int i = 0; i < n; ++i) { m_grid.remove(m_cy); m_grid.insert(m_bottom, blankLine()); }
        }
        break;
    case 'P': {
        TermLine& l = m_grid[m_cy];
        const int n = qMin(arg(0, 1), m_cols - m_cx);
        l.remove(m_cx, n);
        l.resize(m_cols, blankCell());
        break;
    }
    case '@': {
        TermLine& l = m_grid[m_cy];
        const int n = qMin(arg(0, 1), m_cols - m_cx);
        for (int i = 0; i < n; ++i) l.insert(m_cx, blankCell());
        l.resize(m_cols, blankCell());
        break;
    }
    case 'X': {
        TermLine& l = m_grid[m_cy];
        const int n = qMin(arg(0, 1), m_cols - m_cx);
        for (int i = 0; i < n; ++i) l[m_cx + i] = blankCell();
        break;
    }
    case 'S': scrollUp(arg(0, 1)); break;
    case 'T': scrollDown(arg(0, 1)); break;
    case 'm': applySgr(raw); break;
    case 'r':
        m_top = qBound(0, arg(0, 1) - 1, m_rows - 1);
        m_bottom = qBound(0, arg(1, m_rows) - 1, m_rows - 1);
        if (m_top >= m_bottom) { m_top = 0; m_bottom = m_rows - 1; }
        m_cx = 0; m_cy = 0; m_wrapPending = false;
        break;
    case 's': saveCursor(); break;
    case 'u': restoreCursor(); break;
    case 'h':
    case 'l': {
        const bool on = fin == 'h';
        if (prefix == QLatin1Char('?')) {
            for (int v : p) {
                switch (v) {
                case 1: m_appCursor = on; break;
                case 7: m_autowrap = on; break;
                case 25: m_cursorVisible = on; break;
                case 47: case 1047: setAlt(on, false); break;
                case 1049: setAlt(on, true); break;
                case 2004: m_bracketedPaste = on; break;
                default: break;
                }
            }
        }
        break;
    }
    case 'n':
        if (raw0(0) == 5) emit reply("\x1b[0n");
        else if (raw0(0) == 6) {
            const QByteArray pos = QByteArray::number(m_cy + 1) + ';' + QByteArray::number(m_cx + 1) + 'R';
            emit reply(prefix == QLatin1Char('?') ? "\x1b[?" + pos : "\x1b[" + pos);
        }
        break;
    case 'c':
        if (prefix == QLatin1Char('>')) emit reply("\x1b[>0;0;0c");
        else emit reply("\x1b[?1;2c");
        break;
    default:
        break;
    }
}

void TerminalScreen::applySgr(const QStringList& raw)
{
    if (raw.isEmpty()) { m_fg = m_bg = 0; m_attrs = 0; return; }

    const auto colorFrom = [&](const QList<int>& v, int& i, bool fg, bool sub) {
        // v[i] is 38/48; sub = colon form where all parts are in v already.
        const int mode = i + 1 < v.size() ? v[i + 1] : 0;
        quint32 c = 0;
        if (mode == 5 && i + 2 < v.size()) { c = termIndexed(v[i + 2]); i += 2; }
        else if (mode == 2) {
            int base = i + 2;
            if (sub && v.size() - base >= 4) ++base;   // 38:2::r:g:b (colour-space id present)
            if (base + 2 < v.size()) { c = termRgb(v[base], v[base + 1], v[base + 2]); i = base + 2; }
        }
        if (c) (fg ? m_fg : m_bg) = c;
    };

    QList<int> flat;
    for (int idx = 0; idx < raw.size(); ++idx) {
        const QString& tok = raw[idx];
        if (tok.contains(QLatin1Char(':'))) {
            QList<int> sub;
            for (const QString& part : tok.split(QLatin1Char(':'))) sub << part.toInt();
            int i = 0;
            if (!sub.isEmpty() && (sub[0] == 38 || sub[0] == 48)) colorFrom(sub, i, sub[0] == 38, true);
            continue;
        }
        flat << tok.toInt();
    }

    for (int i = 0; i < flat.size(); ++i) {
        const int v = flat[i];
        if (v == 0) { m_fg = m_bg = 0; m_attrs = 0; }
        else if (v == 1) m_attrs |= AttrBold;
        else if (v == 2) m_attrs |= AttrDim;
        else if (v == 3) m_attrs |= AttrItalic;
        else if (v == 4) m_attrs |= AttrUnderline;
        else if (v == 7) m_attrs |= AttrInverse;
        else if (v == 9) m_attrs |= AttrStrike;
        else if (v == 22) m_attrs &= quint8(~(AttrBold | AttrDim));
        else if (v == 23) m_attrs &= quint8(~AttrItalic);
        else if (v == 24) m_attrs &= quint8(~AttrUnderline);
        else if (v == 27) m_attrs &= quint8(~AttrInverse);
        else if (v == 29) m_attrs &= quint8(~AttrStrike);
        else if (v >= 30 && v <= 37) m_fg = termIndexed(v - 30);
        else if (v == 38) colorFrom(flat, i, true, false);
        else if (v == 39) m_fg = 0;
        else if (v >= 40 && v <= 47) m_bg = termIndexed(v - 40);
        else if (v == 48) colorFrom(flat, i, false, false);
        else if (v == 49) m_bg = 0;
        else if (v >= 90 && v <= 97) m_fg = termIndexed(v - 90 + 8);
        else if (v >= 100 && v <= 107) m_bg = termIndexed(v - 100 + 8);
    }
}
