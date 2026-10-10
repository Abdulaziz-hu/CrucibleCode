#pragma once
#include <QFont>
#include <QList>
#include <QStringList>
#include <QWidget>

class QTabWidget;
class QToolButton;
class QMenu;
class TerminalView;

// Tabbed set of native terminals: open as many as you like, close any of them.
class TerminalPanel : public QWidget {
    Q_OBJECT
public:
    struct Shell {
        QString name;
        QString program;
        QStringList args;
    };

    explicit TerminalPanel(QWidget* parent = nullptr);

    static QList<Shell> availableShells();

    void setWorkingDirectory(const QString& dir) { m_cwd = dir; }
    void newTerminal(const QString& workingDir = {});                         // default shell
    void newTerminal(const Shell& shell, const QString& workingDir = {});
    void closeCurrent();
    void closeAll();
    int count() const;
    void setTerminalFont(const QFont& font);
    void applyTheme();
    void focusCurrent();
    QMenu* shellMenu() const { return m_shellMenu; }

signals:
    void countChanged(int count);

private:
    void closeTab(int index);
    void closeView(TerminalView* view);
    void populateShellMenu();
    Shell defaultShell() const;

    QTabWidget* m_tabs = nullptr;
    QToolButton* m_newBtn = nullptr;
    QToolButton* m_menuBtn = nullptr;
    QToolButton* m_closeBtn = nullptr;
    QMenu* m_shellMenu = nullptr;
    QString m_cwd;
    QFont m_font;
};
