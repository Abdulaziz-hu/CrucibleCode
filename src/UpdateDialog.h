#pragma once
#include "Updater.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

// "Update available" popup: release notes, a manual Update button, Later and Skip this version.
// Downloading and applying are driven from here; applied() is emitted once the helper has been
// started and the application should now close.
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(Updater* updater, const ReleaseInfo& release, QWidget* parent = nullptr);

signals:
    void applyRequested(const QString& file);   // the main window saves its files, then calls Updater::applyDownloaded()

private:
    void startUpdate();
    void setBusy(bool busy);

    Updater* m_updater;
    ReleaseInfo m_release;
    ReleaseAsset m_asset;
    bool m_canAuto = false;
    QTextBrowser* m_notes = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_later = nullptr;
    QPushButton* m_skip = nullptr;
    QPushButton* m_page = nullptr;
};
