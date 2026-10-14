#include "UpdateDialog.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(Updater* updater, const ReleaseInfo& release, QWidget* parent)
    : QDialog(parent), m_updater(updater), m_release(release)
{
    setWindowTitle(tr("Update available"));
    resize(640, 520);
    m_canAuto = Updater::installType() != Updater::Unsupported && updater->assetFor(release, &m_asset);

    auto* head = new QLabel(tr("<h3>CrucibleCode %1 is available</h3>You have version %2.")
                                .arg(release.version.toString().toHtmlEscaped(), QCoreApplication::applicationVersion().toHtmlEscaped()));
    m_notes = new QTextBrowser;
    m_notes->setOpenExternalLinks(true);
    const QString notes = release.notes.trimmed().isEmpty() ? tr("(This release has no release notes.)") : release.notes;
    m_notes->setMarkdown(QStringLiteral("## %1\n\n%2").arg(release.title, notes));

    m_status = new QLabel;
    m_status->setWordWrap(true);
    if (!m_canAuto) {
        m_status->setText(Updater::installType() == Updater::Unsupported
            ? tr("This copy was not installed from a release package, so it cannot update itself. Open the release page to download the new version.")
            : tr("This release has no download for your installation type (%1). Open the release page instead.")
                  .arg(Updater::installTypeName(Updater::installType())));
    } else {
        m_status->setText(tr("Update package: %1 (%2 MB). The application will restart automatically after the update.")
                              .arg(m_asset.name).arg(double(m_asset.size) / (1024.0 * 1024.0), 0, 'f', 1));
    }
    m_bar = new QProgressBar;
    m_bar->hide();

    m_update = new QPushButton(tr("Update now"));
    m_update->setDefault(true);
    m_later = new QPushButton(tr("Later"));
    m_skip = new QPushButton(tr("Skip this version"));
    m_page = new QPushButton(tr("Release page"));
    m_update->setVisible(m_canAuto);

    auto* row = new QHBoxLayout;
    row->addWidget(m_page);
    row->addWidget(m_skip);
    row->addStretch(1);
    row->addWidget(m_later);
    row->addWidget(m_update);

    auto* root = new QVBoxLayout(this);
    root->addWidget(head);
    root->addWidget(m_notes, 1);
    root->addWidget(m_status);
    root->addWidget(m_bar);
    root->addLayout(row);

    connect(m_later, &QPushButton::clicked, this, [this] { m_updater->cancelDownload(); reject(); });
    connect(m_skip, &QPushButton::clicked, this, [this] {
        Updater::setSkippedVersion(m_release.tag);
        m_updater->cancelDownload();
        reject();
    });
    connect(m_page, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_release.pageUrl)); });
    connect(m_update, &QPushButton::clicked, this, &UpdateDialog::startUpdate);
    connect(m_updater, &Updater::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_bar->setMaximum(total > 0 ? 1000 : 0);
        if (total > 0) m_bar->setValue(int(1000.0 * double(got) / double(total)));
        m_status->setText(tr("Downloading... %1 / %2 MB").arg(double(got) / 1048576.0, 0, 'f', 1).arg(double(total) / 1048576.0, 0, 'f', 1));
    });
    connect(m_updater, &Updater::statusMessage, this, [this](const QString& t) { m_status->setText(t); });
    connect(m_updater, &Updater::downloadFailed, this, [this](const QString& e) {
        setBusy(false);
        m_status->setText(tr("<b>Download failed:</b> %1").arg(e.toHtmlEscaped()));
    });
    connect(m_updater, &Updater::downloadFinished, this, [this](const QString& file) {
        m_status->setText(tr("Download complete. Restarting to apply the update..."));
        emit applyRequested(file);
        accept();
    });
}

void UpdateDialog::setBusy(bool busy)
{
    m_update->setEnabled(!busy);
    m_skip->setEnabled(!busy);
    m_bar->setVisible(busy);
}

void UpdateDialog::startUpdate()
{
    setBusy(true);
    m_bar->setValue(0);
    m_status->setText(tr("Starting download..."));
    m_updater->download(m_asset, m_release);
}
