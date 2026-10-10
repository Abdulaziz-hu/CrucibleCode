#pragma once
#include <QObject>
#include <QQueue>
#include <QStringList>

class QProcess;

struct StartupItem {
    QString name;   // shown in the toolbar combo box
    QString path;   // executable (CMake) or project file (.NET)
};

struct Diagnostic {
    enum Severity { Error, Warning, Note };
    Severity severity = Error;
    QString file;      // absolute when it could be resolved
    int line = 0;
    int column = 0;
    QString message;
};

// CMake integration: configure / build / rebuild / clean through the cmake CLI,
// compiler-output parsing (GCC, Clang, MSVC, CMake, linker) and a runner for
// the produced executables.
class BuildManager : public QObject {
    Q_OBJECT
public:
    enum Kind { Normal, Info, Warning, Error, Success };
    enum ProjectKind { NoProject, CMakeProject, DotNetProject };

    explicit BuildManager(QObject* parent = nullptr);

    void setProjectRoot(const QString& root);
    QString projectRoot() const { return m_root; }
    bool hasProject() const { return m_kind != NoProject; }
    ProjectKind projectKind() const { return m_kind; }
    QString projectKindName() const;

    void setConfiguration(const QString& c) { m_config = c; }
    QString configuration() const { return m_config; }
    void setCMakePath(const QString& p) { m_cmake = p.trimmed().isEmpty() ? QStringLiteral("cmake") : p.trimmed(); }
    void setGenerator(const QString& g) { m_generator = g.trimmed(); }
    void setExtraArguments(const QString& a) { m_extraArgs = a; }

    QString buildDir() const;
    QString buildRoot() const;
    // CMake: executable targets (CMake File API once configured, add_executable() scan before that,
    // plus executables found in the build folder). .NET: *.csproj files.
    QList<StartupItem> startupItems() const;

    bool isBusy() const;                   // building or running

public slots:
    void configure();
    void build();
    void rebuild();
    void clean();
    void run(const QString& executable);
    void stop();

signals:
    void started(const QString& title);
    void message(const QString& text, BuildManager::Kind kind);
    void diagnostic(const Diagnostic& d);
    void finished(int exitCode, bool success);

private:
    struct Step {
        QString title;
        QString program;
        QStringList args;
    };

    Step configureStep() const;
    QString dotnetTarget() const;
    void ensureFileApiQuery() const;
    QList<StartupItem> fileApiTargets() const;
    QList<StartupItem> scanCMakeLists() const;
    QString resolveExecutable(const QString& path) const;
    Step buildStep(const QString& target = {}, bool cleanFirst = false) const;
    bool needsConfigure() const;
    void begin(const QList<Step>& steps);
    void startNext();
    void consume(const QByteArray& data, QString& buffer);
    void handleLine(QString line);
    QString resolvePath(const QString& file) const;
    static bool parseDiagnostic(const QString& line, Diagnostic* out);

    QProcess* m_proc = nullptr;
    QProcess* m_run = nullptr;
    QQueue<Step> m_queue;
    QString m_outBuf, m_errBuf, m_runOutBuf, m_runErrBuf;
    QString m_root;
    ProjectKind m_kind = NoProject;
    QString m_config = QStringLiteral("Debug");
    QString m_cmake = QStringLiteral("cmake");
    QString m_generator;
    QString m_extraArgs;
    bool m_building = false;
    bool m_hadErrors = false;
};
