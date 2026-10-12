#include "QuickOpenDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

QuickOpenDialog::QuickOpenDialog(const QString& root, QWidget* parent) : QDialog(parent), m_root(root)
{
    setWindowTitle(tr("Go to File"));
    setMinimumSize(560, 380);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    m_edit = new QLineEdit;
    m_edit->setPlaceholderText(tr("Type to search files in the project..."));
    m_list = new QListWidget;
    m_list->setUniformItemSizes(true);
    lay->addWidget(m_edit);
    lay->addWidget(m_list, 1);

    m_edit->installEventFilter(this);
    connect(m_edit, &QLineEdit::textChanged, this, [this] { refilter(); });
    connect(m_edit, &QLineEdit::returnPressed, this, [this] { choose(); });
    connect(m_list, &QListWidget::itemActivated, this, [this] { choose(); });

    scan();
    refilter();
    m_edit->setFocus();
}

void QuickOpenDialog::scan()
{
    static const QStringList skipDirs = {".git", ".svn", ".hg", "node_modules", "CMakeFiles", "_deps",
                                         ".vs", ".idea", "obj", "build", "out", "__pycache__"};
    static const QStringList skipExt = {"exe", "dll", "so",
                                        "dylib", "a", "lib", "o", "obj", "pdb", "zip", "7z", "gz", "tar",
                                        "ttf", "otf", "woff", "woff2", "class", "jar", "bin", "mp3", "mp4", "wav"};
    const QDir base(m_root);
    const auto walk = [&](auto&& self, const QString& dir, int depth) -> void {
        if (depth > 12 || m_files.size() > 50000) return;
        const QFileInfoList entries = QDir(dir).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::DirsFirst);
        for (const QFileInfo& fi : entries) {
            if (fi.isDir()) {
                if (!skipDirs.contains(fi.fileName())) self(self, fi.absoluteFilePath(), depth + 1);
            } else if (!skipExt.contains(fi.suffix().toLower())) {
                m_files << base.relativeFilePath(fi.absoluteFilePath());
            }
        }
    };
    walk(walk, m_root, 0);
}

// Subsequence match with bonuses for word starts, consecutive hits and the file-name part.
int QuickOpenDialog::fuzzyScore(const QString& path, const QString& query)
{
    const QString p = path.toLower();
    const QString q = query.toLower();
    const int nameStart = int(p.lastIndexOf(QLatin1Char('/'))) + 1;
    int score = 0, pi = 0, prev = -2;
    for (const QChar qc : q) {
        if (qc == QLatin1Char(' ')) continue;
        const int hit = int(p.indexOf(qc, pi));
        if (hit < 0) return -1;
        score += 10;
        if (hit == prev + 1) score += 15;
        if (hit == 0 || !p.at(hit - 1).isLetterOrNumber()) score += 12;
        if (hit >= nameStart) score += 6;
        prev = hit;
        pi = hit + 1;
    }
    if (p.mid(nameStart).contains(q)) score += 60;
    return score - int(path.size() / 8);
}

void QuickOpenDialog::refilter()
{
    const QString q = m_edit->text().trimmed();
    QList<QPair<int, QString>> hits;
    for (const QString& f : std::as_const(m_files)) {
        const int s = q.isEmpty() ? 0 : fuzzyScore(f, q);
        if (s >= 0) hits.append({s, f});
    }
    if (!q.isEmpty())
        std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    m_list->clear();
    const int limit = 300;
    for (int i = 0; i < hits.size() && i < limit; ++i) {
        const QString& rel = hits.at(i).second;
        auto* item = new QListWidgetItem(QFileInfo(rel).fileName() + QStringLiteral("    ") + QFileInfo(rel).path());
        item->setData(Qt::UserRole, rel);
        item->setToolTip(rel);
        m_list->addItem(item);
    }
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

void QuickOpenDialog::choose()
{
    if (auto* it = m_list->currentItem()) {
        m_selected = QDir(m_root).filePath(it->data(Qt::UserRole).toString());
        accept();
    }
}

bool QuickOpenDialog::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_edit && ev->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up || key == Qt::Key_PageDown || key == Qt::Key_PageUp) {
            QCoreApplication::sendEvent(m_list, ev);
            return true;
        }
    }
    return QDialog::eventFilter(obj, ev);
}
