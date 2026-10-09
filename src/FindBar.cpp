#include "FindBar.h"
#include "CodeEditor.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextDocument>
#include <QVBoxLayout>

FindBar::FindBar(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(3);

    auto* row1 = new QHBoxLayout;
    row1->setSpacing(4);
    m_find = new QLineEdit;
    m_find->setPlaceholderText(tr("Find"));
    m_find->setMinimumWidth(220);
    auto* prev = new QPushButton(tr("Prev"));
    auto* next = new QPushButton(tr("Next"));
    m_case = new QCheckBox(tr("Match case"));
    m_word = new QCheckBox(tr("Whole word"));
    m_regex = new QCheckBox(tr("Regex"));
    m_status = new QLabel;
    auto* close = new QPushButton(QStringLiteral("x"));
    close->setFixedWidth(24);
    close->setToolTip(tr("Close (Esc)"));
    row1->addWidget(m_find, 1);
    row1->addWidget(prev);
    row1->addWidget(next);
    row1->addWidget(m_case);
    row1->addWidget(m_word);
    row1->addWidget(m_regex);
    row1->addWidget(m_status);
    row1->addStretch(2);
    row1->addWidget(close);
    root->addLayout(row1);

    m_replaceRow = new QWidget;
    auto* row2 = new QHBoxLayout(m_replaceRow);
    row2->setContentsMargins(0, 0, 0, 0);
    row2->setSpacing(4);
    m_replace = new QLineEdit;
    m_replace->setPlaceholderText(tr("Replace with"));
    m_replace->setMinimumWidth(220);
    auto* rep = new QPushButton(tr("Replace"));
    auto* repAll = new QPushButton(tr("Replace All"));
    row2->addWidget(m_replace, 1);
    row2->addWidget(rep);
    row2->addWidget(repAll);
    row2->addStretch(3);
    root->addWidget(m_replaceRow);

    connect(next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(prev, &QPushButton::clicked, this, &FindBar::findPrevious);
    connect(m_find, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(m_find, &QLineEdit::textChanged, this, [this] { m_status->clear(); });
    connect(rep, &QPushButton::clicked, this, &FindBar::replaceOne);
    connect(m_replace, &QLineEdit::returnPressed, this, &FindBar::replaceOne);
    connect(repAll, &QPushButton::clicked, this, &FindBar::replaceAll);
    connect(close, &QPushButton::clicked, this, [this] {
        hide();
        if (m_editor) m_editor->setFocus();
    });

    setStyleSheet(QStringLiteral("FindBar { border-top: 1px solid palette(mid); }"));
    setAutoFillBackground(true);
    hide();
}

void FindBar::setEditor(CodeEditor* editor) { m_editor = editor; }

void FindBar::activate(bool withReplace)
{
    if (m_editor && m_editor->textCursor().hasSelection()) {
        const QString sel = m_editor->textCursor().selectedText();
        if (!sel.contains(QChar::ParagraphSeparator)) m_find->setText(sel);
    }
    m_replaceRow->setVisible(withReplace);
    show();
    m_find->setFocus();
    m_find->selectAll();
}

QTextDocument::FindFlags FindBar::flags() const
{
    QTextDocument::FindFlags f;
    if (m_case->isChecked()) f |= QTextDocument::FindCaseSensitively;
    if (m_word->isChecked()) f |= QTextDocument::FindWholeWords;
    return f;
}

bool FindBar::doFind(bool backward)
{
    if (!m_editor || m_find->text().isEmpty()) return false;

    QTextDocument::FindFlags fl = flags();
    if (backward) fl |= QTextDocument::FindBackward;

    const auto run = [&]() -> bool {
        if (m_regex->isChecked()) {
            QRegularExpression re(m_find->text(),
                m_case->isChecked() ? QRegularExpression::NoPatternOption
                                    : QRegularExpression::CaseInsensitiveOption);
            if (!re.isValid()) return false;
            return m_editor->find(re, fl);
        }
        return m_editor->find(m_find->text(), fl);
    };

    if (m_regex->isChecked() && !QRegularExpression(m_find->text()).isValid()) {
        m_status->setText(tr("Invalid regex"));
        return false;
    }

    const QTextCursor original = m_editor->textCursor();
    bool ok = run();
    if (!ok) {
        QTextCursor c = m_editor->textCursor();
        c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        m_editor->setTextCursor(c);
        ok = run();
        if (ok) m_status->setText(tr("Wrapped"));
    } else {
        m_status->clear();
    }
    if (!ok) {
        m_editor->setTextCursor(original);
        m_status->setText(tr("Not found"));
    }
    return ok;
}

void FindBar::findNext() { doFind(false); }
void FindBar::findPrevious() { doFind(true); }

bool FindBar::selectionMatches(const QString& selected) const
{
    if (selected.isEmpty()) return false;
    if (m_regex->isChecked()) {
        const QRegularExpression re(QRegularExpression::anchoredPattern(m_find->text()),
            m_case->isChecked() ? QRegularExpression::NoPatternOption
                                : QRegularExpression::CaseInsensitiveOption);
        return re.isValid() && re.match(selected).hasMatch();
    }
    return selected.compare(m_find->text(),
        m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
}

void FindBar::replaceOne()
{
    if (!m_editor) return;
    QTextCursor c = m_editor->textCursor();
    if (c.hasSelection() && selectionMatches(c.selectedText()))
        c.insertText(m_replace->text());
    doFind(false);
}

void FindBar::replaceAll()
{
    if (!m_editor || m_find->text().isEmpty()) return;

    QRegularExpression re;
    const bool useRegex = m_regex->isChecked();
    if (useRegex) {
        re.setPattern(m_find->text());
        if (!m_case->isChecked()) re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) { m_status->setText(tr("Invalid regex")); return; }
    }

    QTextDocument* doc = m_editor->document();
    QTextCursor edit(doc);
    edit.beginEditBlock();
    QTextCursor pos(doc);
    int count = 0;
    for (;;) {
        const QTextCursor found = useRegex ? doc->find(re, pos, flags())
                                           : doc->find(m_find->text(), pos, flags());
        if (found.isNull() || !found.hasSelection()) break;
        QTextCursor f = found;
        f.insertText(m_replace->text());
        pos = f;
        ++count;
    }
    edit.endEditBlock();
    m_status->setText(tr("%n replaced", nullptr, count));
}
