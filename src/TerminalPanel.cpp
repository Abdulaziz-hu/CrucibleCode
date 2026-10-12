#include "TerminalPanel.h"
#include "TerminalView.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

QList<TerminalPanel::Shell> TerminalPanel::availableShells()
{
    QList<Shell> list;

#ifdef Q_OS_WIN
    const auto add = [&](const QString& name, const QString& exe, const QStringList& args) {
        const QString p = QStandardPaths::findExecutable(exe);
        if (!p.isEmpty()) list.append({name, p, args});
    };
    add(QStringLiteral("PowerShell 7"), QStringLiteral("pwsh"), {QStringLiteral("-NoLogo")});
    add(QStringLiteral("Windows PowerShell"), QStringLiteral("powershell"), {QStringLiteral("-NoLogo")});
    add(QStringLiteral("Command Prompt"), QStringLiteral("cmd"), {});
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
        const QString base = qEnvironmentVariable(var);
        if (base.isEmpty()) continue;
        const QString bash = base + QStringLiteral("/Git/bin/bash.exe");
        const QString bash2 = base + QStringLiteral("/Programs/Git/bin/bash.exe");
        for (const QString& b : {bash, bash2}) {
            if (QFileInfo::exists(b)) {
                list.append({QStringLiteral("Git Bash"), QDir::toNativeSeparators(b),
                             {QStringLiteral("--login"), QStringLiteral("-i")}});
                goto gitDone;
            }
        }
    }
gitDone:
    add(QStringLiteral("WSL"), QStringLiteral("wsl"), {});
#else
    QStringList candidates;
    const QString envShell = qEnvironmentVariable("SHELL");
    if (!envShell.isEmpty()) candidates << envShell;
    candidates << QStringLiteral("bash") << QStringLiteral("zsh") << QStringLiteral("fish") << QStringLiteral("sh");

    QStringList seen;
    for (const QString& c : std::as_const(candidates)) {
        const QString p = QFileInfo(c).isAbsolute() ? c : QStandardPaths::findExecutable(c);
        if (p.isEmpty() || !QFileInfo(p).isExecutable()) continue;
        const QString canon = QFileInfo(p).canonicalFilePath();
        if (seen.contains(canon)) continue;
        seen << canon;
        QStringList args;
#ifdef Q_OS_MACOS
        const QString n = QFileInfo(p).fileName();
        if (n == QLatin1String("bash") || n == QLatin1String("zsh") || n == QLatin1String("fish"))
            args << QStringLiteral("-l");   // macOS terminals start login shells
#endif
        list.append({QFileInfo(p).fileName(), p, args});
    }
#endif
    return list;
}

TerminalPanel::TerminalPanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    lay->addWidget(m_tabs);

    auto* corner = new QWidget;
    auto* cl = new QHBoxLayout(corner);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);

    m_newBtn = new QToolButton;
    m_newBtn->setText(QStringLiteral("+"));
    m_newBtn->setToolTip(tr("New Terminal"));
    m_menuBtn = new QToolButton;
    m_menuBtn->setText(QStringLiteral("v"));
    m_menuBtn->setToolTip(tr("Choose shell"));
    m_closeBtn = new QToolButton;
    m_closeBtn->setText(QStringLiteral("x"));
    m_closeBtn->setToolTip(tr("Kill Terminal"));
    cl->addWidget(m_newBtn);
    cl->addWidget(m_menuBtn);
    cl->addWidget(m_closeBtn);
    m_tabs->setCornerWidget(corner, Qt::TopRightCorner);

    m_shellMenu = new QMenu(this);
    connect(m_shellMenu, &QMenu::aboutToShow, this, &TerminalPanel::populateShellMenu);
    m_menuBtn->setMenu(m_shellMenu);
    m_menuBtn->setPopupMode(QToolButton::InstantPopup);

    connect(m_newBtn, &QToolButton::clicked, this, [this] { newTerminal(); });
    connect(m_closeBtn, &QToolButton::clicked, this, [this] { closeCurrent(); });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &TerminalPanel::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) { focusCurrent(); });
}

void TerminalPanel::populateShellMenu()
{
    m_shellMenu->clear();
    const QList<Shell> shells = availableShells();
    for (const Shell& s : shells) {
        QAction* a = m_shellMenu->addAction(s.name);
        connect(a, &QAction::triggered, this, [this, s] {
            QSettings().setValue(QStringLiteral("terminal/defaultShell"), s.program);
            newTerminal(s, QString());
        });
    }
    if (shells.isEmpty()) m_shellMenu->addAction(tr("No shells found"))->setEnabled(false);
}

TerminalPanel::Shell TerminalPanel::defaultShell() const
{
    const QList<Shell> shells = availableShells();
    const QString saved = QSettings().value(QStringLiteral("terminal/defaultShell")).toString();
    for (const Shell& s : shells)
        if (s.program == saved) return s;
    return shells.isEmpty() ? Shell{} : shells.first();
}

void TerminalPanel::newTerminal(const QString& workingDir)
{
    newTerminal(defaultShell(), workingDir);
}

void TerminalPanel::newTerminal(const Shell& shell, const QString& workingDir, const QString& labelIn)
{
    if (shell.program.isEmpty()) return;

    auto* view = new TerminalView;
    if (m_font != QFont()) view->setTerminalFont(m_font);

    // unique tab name: "bash", "bash (2)", ...
    int same = 0;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabText(i).startsWith(shell.name)) ++same;
    const QString base = labelIn.isEmpty() ? shell.name : labelIn;
    const QString label = same == 0 || !labelIn.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(shell.name).arg(same + 1);

    const QString wanted = workingDir.isEmpty() ? m_cwd : workingDir;
    const QString cwd = (!wanted.isEmpty() && QFileInfo(wanted).isDir()) ? wanted : QDir::homePath();
    const int idx = m_tabs->addTab(view, label);
    m_tabs->setCurrentIndex(idx);
    view->setShell(shell.program, shell.args, cwd);
    view->setFocus();
    m_lastView = view;

    connect(view, &TerminalView::titleChanged, this, [this, view](const QString& t) {
        const int i = m_tabs->indexOf(view);
        if (i >= 0) m_tabs->setTabToolTip(i, t);
    });
    connect(view, &TerminalView::finished, this, [this, view](int) { closeView(view); });
    emit countChanged(count());
}

QString TerminalPanel::defaultShellProgram() const
{
    return defaultShell().program;
}

void TerminalPanel::runCommand(const QString& title, const QString& command, const QString& workingDir)
{
    m_lastView = nullptr;
    newTerminal(defaultShell(), workingDir, title);
    if (m_lastView) m_lastView->runCommand(command);
}

void TerminalPanel::closeView(TerminalView* view)
{
    const int i = m_tabs->indexOf(view);
    if (i >= 0) closeTab(i);
}

void TerminalPanel::closeTab(int index)
{
    QWidget* w = m_tabs->widget(index);
    if (!w) return;
    m_tabs->removeTab(index);
    w->deleteLater();
    emit countChanged(count());
}

void TerminalPanel::closeCurrent()
{
    if (m_tabs->count() > 0) closeTab(m_tabs->currentIndex());
}

void TerminalPanel::closeAll()
{
    while (m_tabs->count() > 0) closeTab(0);
}

int TerminalPanel::count() const { return m_tabs->count(); }

void TerminalPanel::setTerminalFont(const QFont& font)
{
    m_font = font;
    for (int i = 0; i < m_tabs->count(); ++i)
        static_cast<TerminalView*>(m_tabs->widget(i))->setTerminalFont(font);
}

void TerminalPanel::applyTheme()
{
    for (int i = 0; i < m_tabs->count(); ++i)
        static_cast<TerminalView*>(m_tabs->widget(i))->applyTheme();
}

void TerminalPanel::focusCurrent()
{
    if (QWidget* w = m_tabs->currentWidget()) w->setFocus();
}
