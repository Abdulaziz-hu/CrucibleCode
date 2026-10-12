#include "FindBar.h"
#include "CodeEditor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kMaxHistory = 20;
}

FindBar::FindBar(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(3);

    const auto toolButton = [](const QString& text, const QString& tip, bool checkable) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setAutoRaise(false);
        return b;
    };

    // ---- row 1: [v] [find v] [3 of 12] Aa W .* Prev Next x
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(4);
    m_toggle = toolButton(QStringLiteral(">"), tr("Show / hide Replace (Ctrl+H)"), true);
    m_toggle->setFixedWidth(22);
    m_find = new QComboBox;
    m_find->setEditable(true);
    m_find->setInsertPolicy(QComboBox::NoInsert);
    m_find->setMinimumWidth(240);
    m_find->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_find->lineEdit()->setPlaceholderText(tr("Find"));
    m_status = new QLabel;
    m_status->setMinimumWidth(70);
    m_case = toolButton(QStringLiteral("Aa"), tr("Match case"), true);
    m_word = toolButton(QStringLiteral("W"), tr("Whole word"), true);
    m_regex = toolButton(QStringLiteral(".*"), tr("Regular expression"), true);
    auto* prev = new QPushButton(tr("Prev"));
    auto* next = new QPushButton(tr("Next"));
    auto* close = new QPushButton(QStringLiteral("x"));
    close->setFixedWidth(24);
    close->setToolTip(tr("Close (Esc)"));
    row1->addWidget(m_toggle);
    row1->addWidget(m_find, 1);
    row1->addWidget(m_status);
    row1->addWidget(m_case);
    row1->addWidget(m_word);
    row1->addWidget(m_regex);
    row1->addWidget(prev);
    row1->addWidget(next);
    row1->addStretch(1);
    row1->addWidget(close);
    root->addLayout(row1);

    // ---- row 2: replace
    m_replaceRow = new QWidget;
    auto* row2 = new QHBoxLayout(m_replaceRow);
    row2->setContentsMargins(0, 0, 0, 0);
    row2->setSpacing(4);
    row2->addSpacing(26);   // lines up with the find field
    m_replace = new QComboBox;
    m_replace->setEditable(true);
    m_replace->setInsertPolicy(QComboBox::NoInsert);
    m_replace->setMinimumWidth(240);
    m_replace->lineEdit()->setPlaceholderText(tr("Replace"));
    auto* rep = new QPushButton(tr("Replace"));
    auto* repAll = new QPushButton(tr("Replace All"));
    row2->addWidget(m_replace, 1);
    row2->addWidget(rep);
    row2->addWidget(repAll);
    row2->addStretch(3);
    root->addWidget(m_replaceRow);
    m_replaceRow->hide();

    // ---- wiring
    connect(m_toggle, &QToolButton::toggled, this, [this](bool on) { setReplaceVisible(on); });
    connect(m_find, &QComboBox::editTextChanged, this, [this] { onTextChanged(); });
    connect(m_find->lineEdit(), &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(m_replace->lineEdit(), &QLineEdit::returnPressed, this, &FindBar::replaceOne);
    connect(next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(prev, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(rep, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(repAll, &QPushButton::clicked, this, &FindBar::replaceAll);
    for (QToolButton* b : {m_case, m_word, m_regex})
        connect(b, &QToolButton::toggled, this, [this] { onTextChanged(); });
    connect(close, &QPushButton::clicked, this, [this] {
        hide();
        if (m_editor) m_editor->setFocus();
    });

    setStyleSheet(QStringLiteral("FindBar { border-top: 1px solid palette(mid); }"));
    setAutoFillBackground(true);
    loadHistory();
    hide();
}

// ---- state --------------------------------------------------------------------------------

QString FindBar::findText() const { return m_find->currentText(); }
QString FindBar::replaceText() const { return m_replace->currentText(); }
bool FindBar::useRegex() const { return m_regex->isChecked(); }

QTextDocument::FindFlags FindBar::flags() const
{
    QTextDocument::FindFlags f;
    if (m_case->isChecked()) f |= QTextDocument::FindCaseSensitively;
    if (m_word->isChecked()) f |= QTextDocument::FindWholeWords;
    return f;
}

void FindBar::setReplaceVisible(bool on)
{
    m_replaceRow->setVisible(on);
    m_toggle->setText(on ? QStringLiteral("v") : QStringLiteral(">"));
    if (m_toggle->isChecked() != on) {
        const QSignalBlocker block(m_toggle);
        m_toggle->setChecked(on);
    }
}

void FindBar::setEditor(CodeEditor* editor)
{
    if (m_editor && m_editor != editor) m_editor->clearSearchHighlight();
    m_editor = editor;
    if (isVisible()) applyHighlight();
}

void FindBar::activate(bool withReplace)
{
    if (withReplace) setReplaceVisible(true);
    if (m_editor && m_editor->textCursor().hasSelection()) {
        const QString sel = m_editor->textCursor().selectedText();
        if (!sel.contains(QChar::ParagraphSeparator)) m_find->setEditText(sel);
    }
    show();
    applyHighlight();
    m_find->setFocus();
    m_find->lineEdit()->selectAll();
}

void FindBar::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        if (m_editor) m_editor->setFocus();
        return;
    }
    QWidget::keyPressEvent(e);
}

void FindBar::hideEvent(QHideEvent* e)
{
    QWidget::hideEvent(e);
    if (m_editor) m_editor->clearSearchHighlight();
}

// ---- history ----------------------------------------------------------------------------------

void FindBar::loadHistory()
{
    QSettings s;
    m_find->addItems(s.value(QStringLiteral("find/history")).toStringList());
    m_replace->addItems(s.value(QStringLiteral("find/replaceHistory")).toStringList());
    m_find->setCurrentIndex(-1);
    m_replace->setCurrentIndex(-1);
}

void FindBar::remember(QComboBox* box, const QString& key)
{
    const QString text = box->currentText();
    if (text.isEmpty()) return;
    const QSignalBlocker block(box);
    const int existing = box->findText(text, Qt::MatchExactly);
    if (existing >= 0) box->removeItem(existing);
    box->insertItem(0, text);
    while (box->count() > kMaxHistory) box->removeItem(box->count() - 1);
    box->setCurrentIndex(0);
    QStringList list;
    for (int i = 0; i < box->count(); ++i) list << box->itemText(i);
    QSettings().setValue(key, list);
}

// ---- searching -----------------------------------------------------------------------------------

void FindBar::applyHighlight()
{
    if (!m_editor) return;
    if (findText().isEmpty()) { m_editor->clearSearchHighlight(); updateStatus(); return; }
    m_editor->setSearchHighlight(findText(), m_case->isChecked(), m_word->isChecked(), useRegex());
    updateStatus();
}

void FindBar::updateStatus()
{
    if (!m_editor || findText().isEmpty()) { m_status->clear(); return; }
    if (useRegex() && !QRegularExpression(findText()).isValid()) { m_status->setText(tr("Invalid regex")); return; }
    const int total = m_editor->searchMatchCount();
    if (total == 0) { m_status->setText(tr("No results")); return; }
    const QTextCursor c = m_editor->textCursor();
    const int idx = c.hasSelection() ? m_editor->searchIndexAt(c.selectionStart()) : 0;
    m_status->setText(idx > 0 ? tr("%1 of %2").arg(idx).arg(total) : tr("%1 results").arg(total));
}

// While typing: highlight everything and move to the first match at or after the current one.
void FindBar::onTextChanged()
{
    applyHighlight();
    if (!m_editor || findText().isEmpty()) return;
    QTextCursor c = m_editor->textCursor();
    c.setPosition(c.hasSelection() ? c.selectionStart() : c.position());
    m_editor->setTextCursor(c);
    doFind(false, true);
}

bool FindBar::doFind(bool backward, bool quiet)
{
    if (!m_editor || findText().isEmpty()) return false;
    if (useRegex() && !QRegularExpression(findText()).isValid()) { updateStatus(); return false; }

    QTextDocument::FindFlags fl = flags();
    if (backward) fl |= QTextDocument::FindBackward;

    const auto run = [&]() -> bool {
        if (useRegex()) {
            const QRegularExpression re(findText(), m_case->isChecked() ? QRegularExpression::NoPatternOption
                                                                       : QRegularExpression::CaseInsensitiveOption);
            return m_editor->find(re, fl);
        }
        return m_editor->find(findText(), fl);
    };

    const QTextCursor original = m_editor->textCursor();
    bool ok = run();
    if (!ok) {                                   // wrap around
        QTextCursor c = m_editor->textCursor();
        c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        m_editor->setTextCursor(c);
        ok = run();
    }
    if (!ok) m_editor->setTextCursor(original);
    if (!quiet && ok) remember(m_find, QStringLiteral("find/history"));
    updateStatus();
    return ok;
}

void FindBar::findNext() { doFind(false); }
void FindBar::findPrevious() { doFind(true); }

bool FindBar::selectionMatches(const QString& selected) const
{
    if (selected.isEmpty()) return false;
    if (useRegex()) {
        const QRegularExpression re(QRegularExpression::anchoredPattern(findText()),
            m_case->isChecked() ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        return re.isValid() && re.match(selected).hasMatch();
    }
    return selected.compare(findText(), m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
}

void FindBar::replaceOne()
{
    if (!m_editor) return;
    QTextCursor c = m_editor->textCursor();
    if (c.hasSelection() && selectionMatches(c.selectedText())) {
        c.insertText(replaceText());
        remember(m_replace, QStringLiteral("find/replaceHistory"));
    }
    doFind(false);
}

void FindBar::replaceAll()
{
    if (!m_editor || findText().isEmpty()) return;

    QRegularExpression re;
    if (useRegex()) {
        re.setPattern(findText());
        if (!m_case->isChecked()) re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) { m_status->setText(tr("Invalid regex")); return; }
    }

    QTextDocument* doc = m_editor->document();
    QTextCursor edit(doc);
    edit.beginEditBlock();
    QTextCursor pos(doc);
    int count = 0;
    for (;;) {
        const QTextCursor found = useRegex() ? doc->find(re, pos, flags()) : doc->find(findText(), pos, flags());
        if (found.isNull() || !found.hasSelection()) break;
        QTextCursor f = found;
        f.insertText(replaceText());
        pos = f;
        ++count;
    }
    edit.endEditBlock();
    remember(m_find, QStringLiteral("find/history"));
    remember(m_replace, QStringLiteral("find/replaceHistory"));
    applyHighlight();
    m_status->setText(tr("%n replaced", nullptr, count));
}
