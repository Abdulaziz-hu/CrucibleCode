#include "SearchPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

SearchPanel::SearchPanel(QWidget* parent) : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    auto* row = new QHBoxLayout;
    m_query = new QLineEdit;
    m_query->setPlaceholderText(tr("Find in files"));
    m_case = new QCheckBox(tr("Match case"));
    m_go = new QPushButton(tr("Find All"));
    m_status = new QLabel;
    row->addWidget(m_query, 1);
    row->addWidget(m_case);
    row->addWidget(m_go);
    row->addWidget(m_status);
    root->addLayout(row);

    m_results = new QTreeWidget;
    m_results->setColumnCount(3);
    m_results->setHeaderLabels({tr("Text"), tr("File"), tr("Line")});
    m_results->setRootIsDecorated(false);
    m_results->setUniformRowHeights(true);
    m_results->header()->setStretchLastSection(false);
    m_results->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_results->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_results->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    root->addWidget(m_results, 1);

    connect(m_go, &QPushButton::clicked, this, &SearchPanel::toggleSearch);
    connect(m_query, &QLineEdit::returnPressed, this, &SearchPanel::toggleSearch);
    connect(m_results, &QTreeWidget::itemActivated, this, &SearchPanel::onItemActivated);
}

void SearchPanel::setRoot(const QString& root, const QString& excludedDir)
{
    m_root = root;
    m_excluded = QDir::cleanPath(excludedDir);
    m_results->clear();
    m_status->clear();
}

void SearchPanel::focusQuery(const QString& seed)
{
    if (!seed.isEmpty()) m_query->setText(seed);
    m_query->setFocus();
    m_query->selectAll();
}

void SearchPanel::toggleSearch()
{
    if (m_running) { m_cancel = true; return; }
    runSearch();
}

void SearchPanel::runSearch()
{
    const QString needle = m_query->text();
    if (needle.isEmpty() || m_root.isEmpty()) return;

    m_results->clear();
    m_running = true;
    m_cancel = false;
    m_go->setText(tr("Stop"));

    const Qt::CaseSensitivity cs = m_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    static const QStringList skipDirs = {".git", ".svn", ".hg", "node_modules", "CMakeFiles", ".vs", ".idea"};
    constexpr int kMaxResults = 5000;
    constexpr qint64 kMaxFileSize = 2 * 1024 * 1024;
    int files = 0, hits = 0;

    const auto scan = [&](auto&& self, const QString& dir) -> void {
        const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : entries) {
            if (m_cancel || hits >= kMaxResults) return;
            if (fi.isDir()) {
                if (skipDirs.contains(fi.fileName())) continue;
                if (!m_excluded.isEmpty() && QDir::cleanPath(fi.absoluteFilePath()) == m_excluded) continue;
                self(self, fi.absoluteFilePath());
                continue;
            }
            if (fi.size() > kMaxFileSize) continue;
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray data = f.readAll();
            if (data.left(4096).contains('\0')) continue;
            if ((++files % 25) == 0) {
                m_status->setText(tr("%1 files...").arg(files));
                qApp->processEvents();
            }
            const QString text = QString::fromUtf8(data);
            const QStringList lines = text.split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size() && hits < kMaxResults; ++i) {
                const int col = int(lines.at(i).indexOf(needle, 0, cs));
                if (col < 0) continue;
                auto* item = new QTreeWidgetItem(m_results);
                item->setText(0, lines.at(i).trimmed().left(300));
                item->setText(1, QDir(m_root).relativeFilePath(fi.absoluteFilePath()));
                item->setText(2, QString::number(i + 1));
                item->setData(0, Qt::UserRole, fi.absoluteFilePath());
                item->setData(0, Qt::UserRole + 1, i + 1);
                item->setData(0, Qt::UserRole + 2, col + 1);
                ++hits;
            }
        }
    };
    scan(scan, m_root);

    m_running = false;
    m_go->setText(tr("Find All"));
    m_status->setText(m_cancel ? tr("Cancelled - %1 matches").arg(hits)
                    : hits >= kMaxResults ? tr("%1+ matches (limit reached)").arg(hits)
                                          : tr("%1 matches in %2 files").arg(hits).arg(files));
}

void SearchPanel::onItemActivated(QTreeWidgetItem* item)
{
    emit openRequested(item->data(0, Qt::UserRole).toString(),
                       item->data(0, Qt::UserRole + 1).toInt(),
                       item->data(0, Qt::UserRole + 2).toInt());
}
