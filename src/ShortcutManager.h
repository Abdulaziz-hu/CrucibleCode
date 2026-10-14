#pragma once
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QWidget>

class QAction;
class QKeySequenceEdit;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Registry of every user-configurable command. Each QAction registers itself once with the shortcut
// it was created with (the default); overrides live in QSettings under "shortcuts/<id>".
class ShortcutManager : public QObject {
    Q_OBJECT
public:
    struct Entry {
        QString id;
        QString category;
        QPointer<QAction> action;
        QKeySequence defaultSeq;
    };

    static ShortcutManager& instance();

    void add(const QString& id, QAction* action, const QString& category);
    const QList<Entry>& entries() const { return m_entries; }

    QKeySequence current(const QString& id) const;
    QKeySequence defaultFor(const QString& id) const;
    QString text(const QString& id) const;                  // menu text without & and ...
    bool isCustomized(const QString& id) const;

    void setSequence(const QString& id, const QKeySequence& seq);   // persists; empty = unbound
    void reset(const QString& id);
    void resetAll();

    // Another command that currently uses `seq` (empty id when free).
    QString conflictFor(const QString& id, const QKeySequence& seq) const;

    // JSON import / export of all customised bindings.
    bool exportTo(const QString& path, QString* error = nullptr) const;
    bool importFrom(const QString& path, QString* error = nullptr);

signals:
    void changed();

private:
    ShortcutManager() = default;
    const Entry* find(const QString& id) const;
    void applyTo(const Entry& e);

    QList<Entry> m_entries;
};

// "Shortcuts" page of the settings dialog. Edits are staged and written by apply().
class ShortcutsEditor : public QWidget {
    Q_OBJECT
public:
    explicit ShortcutsEditor(QWidget* parent = nullptr);
    void apply();

private:
    void populate();
    void refreshItem(QTreeWidgetItem* it);
    void filterChanged(const QString& text);
    void selectionChanged();
    void assign();
    void setForSelected(const QKeySequence& seq);
    QKeySequence pendingOrCurrent(const QString& id) const;
    QString pendingConflict(const QString& id, const QKeySequence& seq) const;

    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QKeySequenceEdit* m_edit = nullptr;
    QHash<QString, QKeySequence> m_pending;   // id -> staged sequence
};
