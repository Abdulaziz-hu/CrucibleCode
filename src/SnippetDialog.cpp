#include "SnippetDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QRegularExpression>

SnippetDialog::SnippetDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Snippets"));
    resize(820, 520);
    auto& sm = SnippetManager::instance();
    m_items = sm.builtinSnippets();
    m_items += sm.userSnippets();

    m_list = new QListWidget;
    m_trigger = new QLineEdit;
    m_desc = new QLineEdit;
    m_langs = new QLineEdit;
    m_langs->setPlaceholderText(tr("cpp, h, hpp   or   c++, html, py, *  (empty = all files)"));
    m_body = new QPlainTextEdit;
    m_body->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_body->setTabChangesFocus(false);

    auto* form = new QFormLayout;
    form->addRow(tr("Trigger:"), m_trigger);
    form->addRow(tr("Description:"), m_desc);
    form->addRow(tr("Languages:"), m_langs);
    auto* right = new QVBoxLayout;
    right->addLayout(form);
    right->addWidget(new QLabel(tr("Body:")));
    right->addWidget(m_body, 1);
    auto* help = new QLabel(tr("Type the trigger and press <b>Tab</b>. <tt>$1</tt> <tt>${2:default}</tt> <tt>${3|a,b,c|}</tt> are tab stops "
                               "(same number = mirrored), <tt>$0</tt> is the final cursor position. Variables: "
                               "<tt>$TM_FILENAME</tt> <tt>$TM_FILENAME_BASE</tt> <tt>$CURRENT_YEAR</tt> <tt>$CURRENT_DATE</tt> "
                               "<tt>$CLIPBOARD</tt> <tt>$TM_SELECTED_TEXT</tt>. Use <tt>\\$</tt> for a literal dollar sign."));
    help->setWordWrap(true);
    right->addWidget(help);

    auto* body = new QHBoxLayout;
    body->addWidget(m_list, 2);
    body->addLayout(right, 4);

    auto* add = new QPushButton(tr("New"));
    auto* dup = new QPushButton(tr("Duplicate"));
    auto* del = new QPushButton(tr("Delete"));
    auto* imp = new QPushButton(tr("Import..."));
    auto* exp = new QPushButton(tr("Export..."));
    auto* row = new QHBoxLayout;
    for (auto* b : {add, dup, del}) row->addWidget(b);
    row->addStretch(1);
    row->addWidget(imp);
    row->addWidget(exp);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(body, 1);
    root->addLayout(row);
    root->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { showCurrent(); });
    for (QLineEdit* e : {m_trigger, m_desc, m_langs})
        connect(e, &QLineEdit::textEdited, this, [this] { storeCurrent(); });
    connect(m_body, &QPlainTextEdit::textChanged, this, [this] { storeCurrent(); });

    connect(add, &QPushButton::clicked, this, [this] {
        Snippet s;
        s.trigger = tr("newsnippet");
        s.body = QStringLiteral("$0");
        m_items.append(s);
        reloadList(int(m_items.size()) - 1);
        m_trigger->setFocus();
        m_trigger->selectAll();
    });
    connect(dup, &QPushButton::clicked, this, [this] {
        const int i = currentIndex();
        if (i < 0) return;
        Snippet s = m_items.at(i);
        s.builtin = false;
        if (m_items.at(i).builtin) s.trigger += QStringLiteral("2");
        m_items.append(s);
        reloadList(int(m_items.size()) - 1);
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const int i = currentIndex();
        if (i < 0) return;
        if (m_items.at(i).builtin) {
            QMessageBox::information(this, tr("Snippets"),
                tr("Built-in snippets cannot be deleted. Duplicate one and give it the same trigger to override it."));
            return;
        }
        m_items.removeAt(i);
        reloadList(qMin(i, int(m_items.size()) - 1));
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Import Snippets"), QString(), tr("Snippet files (*.json)"));
        if (p.isEmpty()) return;
        QList<Snippet> in;
        QString err;
        if (!SnippetManager::instance().importFrom(p, &in, &err)) { QMessageBox::warning(this, tr("Import Snippets"), err); return; }
        for (Snippet s : in) { s.builtin = false; m_items.append(s); }
        reloadList(int(m_items.size()) - 1);
    });
    connect(exp, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getSaveFileName(this, tr("Export Snippets"), QStringLiteral("cruciblecode-snippets.json"),
                                                       tr("Snippet files (*.json)"));
        if (p.isEmpty()) return;
        QList<Snippet> mine;
        for (const Snippet& s : m_items) if (!s.builtin) mine << s;
        QString err;
        if (!SnippetManager::instance().exportTo(mine, p, &err)) QMessageBox::warning(this, tr("Export Snippets"), err);
    });
    reloadList(0);
}

int SnippetDialog::currentIndex() const
{
    const int r = m_list->currentRow();
    return (r >= 0 && r < m_items.size()) ? r : -1;
}

void SnippetDialog::reloadList(int select)
{
    m_loading = true;
    m_list->clear();
    for (const Snippet& s : m_items) {
        QString t = s.trigger + QStringLiteral("   ") + s.description;
        if (s.builtin) t += tr("  [built-in]");
        m_list->addItem(t);
    }
    m_loading = false;
    m_list->setCurrentRow(qBound(0, select, qMax(0, int(m_items.size()) - 1)));
    showCurrent();
}

void SnippetDialog::showCurrent()
{
    if (m_loading) return;
    const int i = currentIndex();
    m_loading = true;
    const bool ok = i >= 0;
    const Snippet s = ok ? m_items.at(i) : Snippet();
    m_trigger->setText(s.trigger);
    m_desc->setText(s.description);
    m_langs->setText(s.languages.join(QStringLiteral(", ")));
    m_body->setPlainText(s.body);
    for (QWidget* w : {static_cast<QWidget*>(m_trigger), static_cast<QWidget*>(m_desc), static_cast<QWidget*>(m_langs)})
        static_cast<QLineEdit*>(w)->setReadOnly(!ok || s.builtin);
    m_body->setReadOnly(!ok || s.builtin);
    m_loading = false;
}

void SnippetDialog::storeCurrent()
{
    if (m_loading) return;
    const int i = currentIndex();
    if (i < 0 || m_items.at(i).builtin) return;
    Snippet& s = m_items[i];
    s.trigger = m_trigger->text().trimmed();
    s.description = m_desc->text();
    s.body = m_body->toPlainText();
    s.languages.clear();
    for (const QString& l : m_langs->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) s.languages << l.trimmed();
    if (auto* it = m_list->item(i)) it->setText(s.trigger + QStringLiteral("   ") + s.description);
}

void SnippetDialog::accept()
{
    QList<Snippet> mine;
    for (const Snippet& s : m_items) {
        if (s.builtin) continue;
        if (s.trigger.isEmpty() || s.trigger.contains(QRegularExpression(QStringLiteral("\\s")))) {
            QMessageBox::warning(this, tr("Snippets"), tr("Every snippet needs a trigger without spaces."));
            return;
        }
        mine << s;
    }
    SnippetManager::instance().setUserSnippets(mine);
    QDialog::accept();
}
