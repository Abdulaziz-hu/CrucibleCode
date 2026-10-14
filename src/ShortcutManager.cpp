#include "ShortcutManager.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

ShortcutManager& ShortcutManager::instance()
{
    static ShortcutManager m;
    return m;
}

const ShortcutManager::Entry* ShortcutManager::find(const QString& id) const
{
    for (const Entry& e : m_entries)
        if (e.id == id) return &e;
    return nullptr;
}

void ShortcutManager::add(const QString& id, QAction* action, const QString& category)
{
    if (!action || find(id)) return;
    Entry e;
    e.id = id;
    e.category = category;
    e.action = action;
    e.defaultSeq = action->shortcut();
    m_entries.append(e);
    applyTo(e);
}

void ShortcutManager::applyTo(const Entry& e)
{
    if (!e.action) return;
    QSettings s;
    const QString key = QStringLiteral("shortcuts/") + e.id;
    if (s.contains(key))
        e.action->setShortcut(QKeySequence::fromString(s.value(key).toString(), QKeySequence::PortableText));
    else
        e.action->setShortcut(e.defaultSeq);
}

QKeySequence ShortcutManager::current(const QString& id) const
{
    const Entry* e = find(id);
    return (e && e->action) ? e->action->shortcut() : QKeySequence();
}

QKeySequence ShortcutManager::defaultFor(const QString& id) const
{
    const Entry* e = find(id);
    return e ? e->defaultSeq : QKeySequence();
}

QString ShortcutManager::text(const QString& id) const
{
    const Entry* e = find(id);
    if (!e || !e->action) return id;
    QString t = e->action->text();
    t.remove(QLatin1Char('&'));
    if (t.endsWith(QLatin1String("..."))) t.chop(3);
    return t;
}

bool ShortcutManager::isCustomized(const QString& id) const
{
    return QSettings().contains(QStringLiteral("shortcuts/") + id);
}

void ShortcutManager::setSequence(const QString& id, const QKeySequence& seq)
{
    const Entry* e = find(id);
    if (!e) return;
    QSettings s;
    const QString key = QStringLiteral("shortcuts/") + id;
    if (seq == e->defaultSeq) s.remove(key);
    else s.setValue(key, seq.toString(QKeySequence::PortableText));
    applyTo(*e);
    emit changed();
}

void ShortcutManager::reset(const QString& id)
{
    const Entry* e = find(id);
    if (!e) return;
    QSettings().remove(QStringLiteral("shortcuts/") + id);
    applyTo(*e);
    emit changed();
}

void ShortcutManager::resetAll()
{
    QSettings().remove(QStringLiteral("shortcuts"));
    for (const Entry& e : m_entries) applyTo(e);
    emit changed();
}

QString ShortcutManager::conflictFor(const QString& id, const QKeySequence& seq) const
{
    if (seq.isEmpty()) return {};
    for (const Entry& e : m_entries)
        if (e.id != id && e.action && e.action->shortcut() == seq) return e.id;
    return {};
}

bool ShortcutManager::exportTo(const QString& path, QString* error) const
{
    QJsonObject o;
    for (const Entry& e : m_entries) {
        if (!isCustomized(e.id)) continue;
        o.insert(e.id, current(e.id).toString(QKeySequence::PortableText));
    }
    QJsonObject root{{QStringLiteral("format"), QStringLiteral("cruciblecode-shortcuts")},
                     {QStringLiteral("version"), 1}, {QStringLiteral("bindings"), o}};
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

bool ShortcutManager::importFrom(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (d.isNull() || !d.isObject() || d.object().value(QStringLiteral("format")).toString() != QLatin1String("cruciblecode-shortcuts")) {
        if (error) *error = tr("Not a CrucibleCode shortcuts file.");
        return false;
    }
    const QJsonObject b = d.object().value(QStringLiteral("bindings")).toObject();
    for (auto it = b.begin(); it != b.end(); ++it)
        if (find(it.key())) setSequence(it.key(), QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText));
    return true;
}

// ---------------------------------------------------------------------------------------------
//  Settings page
// ---------------------------------------------------------------------------------------------

ShortcutsEditor::ShortcutsEditor(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Search commands or shortcuts"));
    m_filter->setClearButtonEnabled(true);
    root->addWidget(m_filter);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({tr("Command"), tr("Category"), tr("Shortcut"), tr("Default")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    root->addWidget(m_tree, 1);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("New shortcut:")));
    m_edit = new QKeySequenceEdit;
    row->addWidget(m_edit, 1);
    auto* assignBtn = new QPushButton(tr("Assign"));
    auto* clearBtn = new QPushButton(tr("Unbind"));
    auto* resetBtn = new QPushButton(tr("Reset"));
    row->addWidget(assignBtn);
    row->addWidget(clearBtn);
    row->addWidget(resetBtn);
    root->addLayout(row);

    auto* row2 = new QHBoxLayout;
    auto* resetAll = new QPushButton(tr("Reset All"));
    auto* exportBtn = new QPushButton(tr("Export..."));
    auto* importBtn = new QPushButton(tr("Import..."));
    row2->addWidget(resetAll);
    row2->addStretch(1);
    row2->addWidget(importBtn);
    row2->addWidget(exportBtn);
    root->addLayout(row2);

    populate();
    connect(m_filter, &QLineEdit::textChanged, this, &ShortcutsEditor::filterChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ShortcutsEditor::selectionChanged);
    connect(assignBtn, &QPushButton::clicked, this, &ShortcutsEditor::assign);
    connect(m_edit, &QKeySequenceEdit::editingFinished, this, &ShortcutsEditor::assign);
    connect(clearBtn, &QPushButton::clicked, this, [this] { setForSelected(QKeySequence()); });
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        auto* it = m_tree->currentItem();
        if (it) setForSelected(ShortcutManager::instance().defaultFor(it->data(0, Qt::UserRole).toString()));
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        for (const auto& e : ShortcutManager::instance().entries()) m_pending.insert(e.id, e.defaultSeq);
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) refreshItem(m_tree->topLevelItem(i));
    });
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        apply();
        const QString p = QFileDialog::getSaveFileName(this, tr("Export Shortcuts"),
                              QStringLiteral("cruciblecode-shortcuts.json"), tr("Shortcut files (*.json)"));
        if (p.isEmpty()) return;
        QString err;
        if (!ShortcutManager::instance().exportTo(p, &err)) QMessageBox::warning(this, tr("Export Shortcuts"), err);
    });
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Import Shortcuts"), QString(), tr("Shortcut files (*.json)"));
        if (p.isEmpty()) return;
        QString err;
        if (!ShortcutManager::instance().importFrom(p, &err)) { QMessageBox::warning(this, tr("Import Shortcuts"), err); return; }
        m_pending.clear();
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) refreshItem(m_tree->topLevelItem(i));
    });
}

void ShortcutsEditor::populate()
{
    auto& sm = ShortcutManager::instance();
    for (const auto& e : sm.entries()) {
        auto* it = new QTreeWidgetItem(m_tree);
        it->setData(0, Qt::UserRole, e.id);
        refreshItem(it);
    }
    m_tree->sortItems(1, Qt::AscendingOrder);
}

QKeySequence ShortcutsEditor::pendingOrCurrent(const QString& id) const
{
    const auto it = m_pending.constFind(id);
    return it != m_pending.constEnd() ? it.value() : ShortcutManager::instance().current(id);
}

QString ShortcutsEditor::pendingConflict(const QString& id, const QKeySequence& seq) const
{
    if (seq.isEmpty()) return {};
    for (const auto& e : ShortcutManager::instance().entries())
        if (e.id != id && pendingOrCurrent(e.id) == seq) return e.id;
    return {};
}

void ShortcutsEditor::refreshItem(QTreeWidgetItem* it)
{
    auto& sm = ShortcutManager::instance();
    const QString id = it->data(0, Qt::UserRole).toString();
    const QKeySequence cur = pendingOrCurrent(id);
    const QKeySequence def = sm.defaultFor(id);
    it->setText(0, sm.text(id));
    for (const auto& e : sm.entries()) if (e.id == id) it->setText(1, e.category);
    it->setText(2, cur.toString(QKeySequence::NativeText));
    it->setText(3, def.toString(QKeySequence::NativeText));
    QFont f = it->font(2);
    f.setBold(cur != def);
    it->setFont(2, f);
}

void ShortcutsEditor::filterChanged(const QString& text)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* it = m_tree->topLevelItem(i);
        bool match = text.isEmpty();
        for (int c = 0; c < 3 && !match; ++c) match = it->text(c).contains(text, Qt::CaseInsensitive);
        it->setHidden(!match);
    }
}

void ShortcutsEditor::selectionChanged()
{
    m_edit->clear();
    if (auto* it = m_tree->currentItem()) m_edit->setKeySequence(QKeySequence::fromString(it->text(2), QKeySequence::NativeText));
}

void ShortcutsEditor::assign()
{
    if (m_tree->currentItem() && !m_edit->keySequence().isEmpty()) setForSelected(m_edit->keySequence());
}

void ShortcutsEditor::setForSelected(const QKeySequence& seq)
{
    auto* it = m_tree->currentItem();
    if (!it) return;
    auto& sm = ShortcutManager::instance();
    const QString id = it->data(0, Qt::UserRole).toString();
    const QString other = pendingConflict(id, seq);
    if (!other.isEmpty()) {
        const auto r = QMessageBox::question(this, tr("Shortcut in use"),
            tr("%1 is already used by \"%2\".\nReassign it to \"%3\"?")
                .arg(seq.toString(QKeySequence::NativeText), sm.text(other), sm.text(id)));
        if (r != QMessageBox::Yes) return;
        m_pending.insert(other, QKeySequence());
    }
    m_pending.insert(id, seq);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) refreshItem(m_tree->topLevelItem(i));
    selectionChanged();
}

void ShortcutsEditor::apply()
{
    auto& sm = ShortcutManager::instance();
    // clear first so that swapping two shortcuts never leaves a transient duplicate
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) sm.setSequence(it.key(), QKeySequence());
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) sm.setSequence(it.key(), it.value());
    m_pending.clear();
}
