#pragma once
#include <QObject>
#include <QStringConverter>
#include <QVector>

#include <deque>

// Colour encoding: top byte = kind (0 default, 1 indexed 0..255, 2 RGB), low 24 bits = value.
inline quint32 termIndexed(int i) { return (1u << 24) | quint32(i & 0xFF); }
inline quint32 termRgb(int r, int g, int b) { return (2u << 24) | (quint32(r & 0xFF) << 16) | (quint32(g & 0xFF) << 8) | quint32(b & 0xFF); }

enum TermAttr : quint8 { AttrBold = 1, AttrDim = 2, AttrItalic = 4, AttrUnderline = 8, AttrInverse = 16, AttrStrike = 32 };

struct TermCell {
    char32_t ch = U' ';
    quint32 fg = 0;
    quint32 bg = 0;
    quint8 attrs = 0;
};
using TermLine = QVector<TermCell>;

// VT100 / xterm compatible terminal emulator core (no GUI): feed it the bytes a
// program prints, read back a grid of cells plus scrollback.
class TerminalScreen : public QObject {
    Q_OBJECT
public:
    explicit TerminalScreen(int cols = 80, int rows = 24, QObject* parent = nullptr);

    void write(const QByteArray& data);
    void resize(int cols, int rows);
    void clearScrollback();
    void fullReset();

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    int scrollbackSize() const { return int(m_scrollback.size()); }
    const TermLine& lineAt(int index) const;     // 0 .. scrollbackSize()+rows()-1

    int cursorX() const { return m_cx; }
    int cursorY() const { return m_cy; }
    bool cursorVisible() const { return m_cursorVisible; }
    bool appCursorKeys() const { return m_appCursor; }
    bool bracketedPaste() const { return m_bracketedPaste; }
    bool altScreen() const { return m_alt; }

signals:
    void titleChanged(const QString& title);
    void reply(const QByteArray& bytes);   // answers to terminal queries (cursor position, ...)

private:
    enum State { Ground, Esc, Csi, Osc, OscEsc, Charset };

    void process(char32_t c);
    void control(char32_t c);
    void putChar(char32_t c);
    void executeCsi(char32_t fin);
    void applySgr(const QStringList& raw);
    void finishString();

    void lineFeed();
    void reverseIndex();
    void scrollUp(int n);
    void scrollDown(int n);
    void eraseLine(int mode);
    void eraseDisplay(int mode);
    void setAlt(bool on, bool saveCursor);
    void saveCursor();
    void restoreCursor();

    static TermLine compactForScrollback(const TermLine& line);
    void pushScrollback(const TermLine& line);

    TermCell blankCell() const;
    TermLine blankLine() const;
    QVector<TermLine> blankGrid() const;
    void clampCursor();

    int m_cols, m_rows;
    QVector<TermLine> m_grid, m_altGrid;
    std::deque<TermLine> m_scrollback;
    static constexpr int kMaxScrollback = 5000;

    int m_cx = 0, m_cy = 0;
    int m_savedX = 0, m_savedY = 0;
    quint32 m_savedFg = 0, m_savedBg = 0;
    quint8 m_savedAttrs = 0;
    int m_top = 0, m_bottom = 0;
    bool m_wrapPending = false;
    bool m_autowrap = true;
    bool m_cursorVisible = true;
    bool m_appCursor = false;
    bool m_bracketedPaste = false;
    bool m_alt = false;

    quint32 m_fg = 0, m_bg = 0;
    quint8 m_attrs = 0;

    State m_state = Ground;
    QString m_csi;
    QString m_str;
    bool m_strIsOsc = false;

    QStringDecoder m_decoder{QStringDecoder::Utf8};
};
