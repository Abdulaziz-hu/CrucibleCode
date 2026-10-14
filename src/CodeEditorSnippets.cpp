// Snippet expansion with tab stops and mirrored placeholders (part of CodeEditor).
#include "CodeEditor.h"
#include "Snippets.h"
#include "ThemeManager.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTextCursor>
#include <QTimer>
#include <algorithm>

bool CodeEditor::expandSnippetAtCursor()
{
    const QTextCursor c = textCursor();
    if (c.hasSelection()) return false;
    const QString line = c.block().text();
    int end = c.positionInBlock(), start = end;
    while (start > 0 && (line.at(start - 1).isLetterOrNumber() || line.at(start - 1) == QLatin1Char('_'))) --start;
    if (start == end) return false;
    const QString word = line.mid(start, end - start);
    Snippet snip;
    if (!SnippetManager::instance().find(m_path.isEmpty() ? m_untitled : m_path, word, &snip)) return false;

    QTextCursor sel = c;
    sel.setPosition(c.block().position() + start);
    sel.setPosition(c.block().position() + end, QTextCursor::KeepAnchor);
    sel.removeSelectedText();                      // the trigger word goes away, the body replaces it
    setTextCursor(sel);
    insertSnippet(snip.body);
    return true;
}

void CodeEditor::insertSnippet(const QString& body)
{
    endSnippet();
    QTextCursor c = textCursor();
    SnippetContext ctx;
    ctx.fileName = QFileInfo(m_path).fileName();
    ctx.selectedText = c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    ctx.clipboard = QGuiApplication::clipboard()->text();

    const QString blockText = c.block().text();
    int ws = 0;
    while (ws < blockText.size() && blockText.at(ws).isSpace()) ++ws;
    const QString indent = blockText.left(ws);

    const ExpandedSnippet ex = SnippetManager::expand(body, indent, m_tabSize, ctx);
    const int start = c.selectionStart();

    m_snipSync = true;
    c.beginEditBlock();
    c.insertText(ex.text);
    c.endEditBlock();
    m_snipSync = false;

    if (ex.stops.isEmpty()) { setTextCursor(c); return; }

    // group the ranges per stop index; the final position ($0) is visited last
    QList<SnipStop> stops;
    for (const auto& s : ex.stops) {
        SnipStop* st = nullptr;
        for (SnipStop& x : stops) if (x.index == s.index) st = &x;
        if (!st) { stops.append(SnipStop{s.index, {}}); st = &stops.last(); }
        st->ranges.append({start + s.start, start + s.end});
    }
    const auto order = [](const SnipStop& a, const SnipStop& b) {
        if (a.index == 0) return false;
        if (b.index == 0) return true;
        return a.index < b.index;
    };
    std::stable_sort(stops.begin(), stops.end(), order);
    if (stops.last().index != 0) stops.append(SnipStop{0, {{start + int(ex.text.size()), start + int(ex.text.size())}}});

    if (stops.size() == 1) {                       // only $0: just place the cursor
        QTextCursor fin = textCursor();
        fin.setPosition(stops.first().ranges.first().start);
        setTextCursor(fin);
        return;
    }
    m_snipStops = stops;
    m_snipWhole = {start, start + int(ex.text.size())};
    m_snipActive = true;
    snipGoto(0);
}

void CodeEditor::snipGoto(int stop)
{
    if (stop < 0 || stop >= m_snipStops.size()) { endSnippet(); return; }
    m_snipCur = stop;
    const SnipRange r = m_snipStops.at(stop).ranges.first();
    QTextCursor c = textCursor();
    m_snipSync = true;
    c.setPosition(r.start);
    c.setPosition(r.end, QTextCursor::KeepAnchor);
    setTextCursor(c);
    m_snipSync = false;
    centerCursor();
    if (m_snipStops.at(stop).index == 0) { endSnippet(); return; }
    snipUpdateSelections();
}

bool CodeEditor::snipMove(bool backwards)
{
    if (!m_snipActive) return false;
    const int next = m_snipCur + (backwards ? -1 : 1);
    if (next < 0) return true;                     // already on the first stop: swallow Shift+Tab
    snipGoto(next);
    return true;
}

void CodeEditor::endSnippet()
{
    if (!m_snipActive && m_snipSelections.isEmpty()) return;
    m_snipActive = false;
    m_snipStops.clear();
    m_snipCur = -1;
    m_snipSelections.clear();
    rebuildExtraSelections();
}

void CodeEditor::snipUpdateSelections()
{
    m_snipSelections.clear();
    if (m_snipActive) {
        QColor col = ThemeManager::instance().colors().selection;
        col.setAlpha(110);
        for (int i = 0; i < m_snipStops.size(); ++i) {
            if (m_snipStops.at(i).index == 0) continue;
            for (const SnipRange& r : m_snipStops.at(i).ranges) {
                if (r.end <= r.start) continue;
                QTextEdit::ExtraSelection s;
                s.cursor = QTextCursor(document());
                s.cursor.setPosition(r.start);
                s.cursor.setPosition(r.end, QTextCursor::KeepAnchor);
                s.format.setBackground(col);
                m_snipSelections.append(s);
            }
        }
    }
    rebuildExtraSelections();
}

// Keeps every range in step with edits. Returns true when the edit happened inside the active stop.
bool CodeEditor::snipAdjust(int pos, int removed, int added)
{
    const int delta = added - removed;
    bool touched = false, broken = false;
    const auto fix = [&](SnipRange& r) -> bool {
        if (pos >= r.start && pos + removed <= r.end) { r.end += delta; return true; }
        if (pos + removed <= r.start) { r.start += delta; r.end += delta; return false; }
        if (pos >= r.end) return false;
        broken = true;
        return false;
    };
    for (int i = 0; i < m_snipStops.size(); ++i)
        for (SnipRange& r : m_snipStops[i].ranges)
            if (fix(r) && i == m_snipCur) touched = true;
    fix(m_snipWhole);
    if (broken) endSnippet();
    return touched;
}

void CodeEditor::onContentsChange(int pos, int removed, int added)
{
    if (!m_snipActive || m_snipSync) return;
    if (snipAdjust(pos, removed, added) && m_snipActive && m_snipStops.at(m_snipCur).ranges.size() > 1 && !m_snipDirty) {
        m_snipDirty = true;
        QTimer::singleShot(0, this, [this] { snipSyncMirrors(); });   // never edit inside the change notification
    } else if (m_snipActive) {
        snipUpdateSelections();
    }
}

void CodeEditor::snipSyncMirrors()
{
    m_snipDirty = false;
    if (!m_snipActive || m_snipCur < 0 || m_snipCur >= m_snipStops.size()) return;
    const QList<SnipRange> ranges = m_snipStops.at(m_snipCur).ranges;
    if (ranges.size() < 2) return;
    const SnipRange src = ranges.first();
    const QString text = toPlainText().mid(src.start, src.end - src.start);

    const QTextCursor saved = textCursor();
    const int savedPos = saved.position(), savedAnchor = saved.anchor();
    // edit the mirrors from the end of the document backwards so earlier offsets stay valid
    QList<int> idx;
    for (int i = 1; i < ranges.size(); ++i) idx << i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return ranges.at(a).start > ranges.at(b).start; });
    int shift = 0;   // how far the caret moves because of edits in front of it
    m_snipSync = true;
    QTextCursor edit(document());
    edit.joinPreviousEditBlock();
    for (int i : idx) {
        const SnipRange cur = m_snipStops.at(m_snipCur).ranges.at(i);
        edit.setPosition(cur.start);
        edit.setPosition(cur.end, QTextCursor::KeepAnchor);
        const int oldLen = cur.end - cur.start;
        edit.insertText(text);
        m_snipSync = false;
        snipAdjust(cur.start, oldLen, int(text.size()));
        m_snipSync = true;
        if (cur.start < savedPos) shift += int(text.size()) - oldLen;
    }
    edit.endEditBlock();
    m_snipSync = false;

    QTextCursor back = textCursor();
    back.setPosition(savedAnchor + shift);
    back.setPosition(savedPos + shift, QTextCursor::KeepAnchor);
    m_snipSync = true;
    setTextCursor(back);
    m_snipSync = false;
    snipUpdateSelections();
}
