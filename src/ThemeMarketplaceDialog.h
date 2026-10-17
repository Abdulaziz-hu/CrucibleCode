#pragma once
#include "ThemeLibrary.h"
#include "ThemeManager.h"
#include "ThemeMarketplace.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTabBar;

// A miniature IDE painted with the colours of a theme: menu bar, Explorer with Git colours, tabs,
// code with syntax colours, current line, selection, find match, Git gutter markers, output panel
// and the status bar. Lets people judge a theme before installing it.
class ThemePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ThemePreviewWidget(QWidget* parent = nullptr);
    void setColors(const ThemeColors& c);
    void setMessage(const QString& text);     // shown instead of a preview (loading, error, ...)
    QSize sizeHint() const override { return {460, 320}; }
    QSize minimumSizeHint() const override { return {320, 230}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    ThemeColors m_colors;
    bool m_has = false;
    QString m_message;
};

// "Theme Marketplace": browse the community themes of the CrucibleCode-Themes repository, install and
// uninstall them, apply and unapply them, and manage the built-in and installed themes.
// Applying a theme takes effect immediately and is remembered.
class ThemeMarketplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeMarketplaceDialog(QWidget* parent = nullptr);

private:
    enum Tab { MarketplaceTab = 0, InstalledTab = 1, BuiltInTab = 2 };
    enum class Action { None, Install, Apply, Update };

    struct Row {
        QString listKey;                 // unique in the list
        QString id;
        QString key;                     // what ThemeManager applies once installed ("installed:x", "builtin:x", "dark", ...)
        QString name, author, version, description, type, credits;
        QStringList tags;
        bool builtin = false;
        bool remote = false;             // listed in the marketplace index
        bool installed = false;
        bool local = false;              // installed from a file
        QString installedVersion, remoteVersion;
    };

    void buildUi();
    void rebuildList();
    const Row* currentRow() const;
    void updateDetails();
    void updateStatusStyles();
    bool colorsFor(const Row& r, ThemeColors* out);
    QIcon iconFor(const Row& r);
    QString statusFor(const Row& r) const;
    bool isApplied(const Row& r) const;
    static QString remoteKey(const QString& id, const QString& version) { return id + QLatin1Char('@') + version; }

    // marketplace
    void loadIndex();
    void onIndexLoaded(int ignored);
    void onIndexFailed(const QString& error);
    void onThemeFetched(const QString& id, const QByteArray& css);
    void onThemeFailed(const QString& id, const QString& error);
    void ensureRemote(const Row& r);
    void startPrefetch();
    void pumpPrefetch();

    // actions
    void startAction(Action a);
    void completeAction(Action a, const QString& id);
    void applyKey(const QString& key);
    void unapply(const Row& r);
    void uninstall(const Row& r);
    void installFromFile();
    void setBusy(bool busy, const QString& text = QString());

    QLineEdit* m_search = nullptr;
    QComboBox* m_type = nullptr;
    QPushButton* m_refresh = nullptr;
    QTabBar* m_tabs = nullptr;
    QListWidget* m_list = nullptr;
    ThemePreviewWidget* m_preview = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_meta = nullptr;
    QLabel* m_state = nullptr;
    QLabel* m_description = nullptr;
    QLabel* m_credits = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_install = nullptr;
    QPushButton* m_uninstall = nullptr;
    QPushButton* m_apply = nullptr;
    QPushButton* m_unapply = nullptr;
    QPushButton* m_update = nullptr;

    ThemeMarketplace* m_market = nullptr;
    QVector<Row> m_rows;
    QHash<QString, ThemeColors> m_colorCache;       // installed / built-in themes by key
    QHash<QString, QByteArray> m_remoteCss;         // downloaded marketplace files by id@version
    QHash<QString, ThemeColors> m_remoteColors;
    QHash<QString, QString> m_failed;               // id@version -> error text
    QList<MarketTheme> m_prefetch;
    QString m_prefetchId;
    QString m_pendingId;
    Action m_pending = Action::None;
    bool m_busy = false;
    bool m_loadingIndex = false;
    bool m_offline = false;
    int m_total[3] = {0, 0, 0};
};
