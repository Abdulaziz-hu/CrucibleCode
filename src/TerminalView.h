#pragma once
#include <QFont>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class PtyProcess;
class QKeyEvent;
class QScrollBar;
class TerminalScreen;

// A single terminal: pseudo terminal process + emulator + painter + input handling.
class TerminalView : public QWidget {
    Q_OBJECT
public:
    explicit TerminalView(QWidget* parent = nullptr);
    ~TerminalView() override;

    void setShell(const QString& program, const QStringList& args, const QString& workingDir);
    void setTerminalFont(const QFont& font);
    void applyTheme();

    bool hasSelection() const { return m_hasSel; }
    void copySelection();
    void pasteClipboard();
    void selectAll();
    void clearScreen();
    QString title() const { return m_title; }

signals:
    void titleChanged(const QString& title);
    void finished(int exitCode);

protected:
    bool event(QEvent* e) override;
    bool focusNextPrevChild(bool next) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void focusInEvent(QFocusEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    struct CellPos { int line = 0; int col = 0; };

    void maybeStart();
    void recomputeGrid();
    void onData(const QByteArray& data);
    void updateScrollBar();
    void sendText(const QByteArray& bytes);
    QByteArray encodeKey(QKeyEvent* e) const;
    CellPos cellAt(const QPoint& p) const;
    bool inSelection(int line, int col) const;
    QString selectedText() const;
    QColor resolve(quint32 code, bool isFg) const;
    void drawLine(QPainter& p, int row, int lineIndex);
    void buildFonts(const QFont& f);

    TerminalScreen* m_screen = nullptr;
    PtyProcess* m_pty = nullptr;
    QScrollBar* m_bar = nullptr;
    QTimer m_repaint;
    QTimer m_blink;

    QString m_program, m_cwd, m_title;
    QStringList m_args;
    bool m_pendingStart = false;
    bool m_exited = false;

    QFont m_font, m_fontBold, m_fontItalic, m_fontBoldItalic;
    int m_cw = 8, m_ch = 16, m_ascent = 12;
    int m_cols = 80, m_rows = 24;
    int m_offset = 0;           // lines scrolled back from the bottom
    bool m_cursorOn = true;

    bool m_selecting = false, m_hasSel = false;
    CellPos m_selA, m_selB;
};
