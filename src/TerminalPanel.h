#pragma once
#include <QFont>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "TerminalView.h"

class QTabWidget;
class QToolButton;
class QMenu;

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
    void newTerminal(const Shell& shell, const QString& workingDir = {}, const QString& label = {});
    // Opens a terminal in workingDir, titled `title`, and types `command` into it.
    void runCommand(const QString& title, const QString& command, const QString& workingDir);
    QString defaultShellProgram() const;
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
    QPointer<TerminalView> m_lastView;
    QFont m_font;
};
