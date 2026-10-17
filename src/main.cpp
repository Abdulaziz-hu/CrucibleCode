#include "MainWindow.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QSettings>
#include <QTimer>

#include "MemTrim.h"

#if defined(__GLIBC__)
#  include <malloc.h>
#endif

static QIcon makeAppIcon()
{
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512})
        icon.addFile(QStringLiteral(":/icons/icon-%1.png").arg(size), QSize(size, size));
    return icon;
}

int main(int argc, char* argv[])
{
    // ---- memory: allocator tuning (glibc) ---------------------------------------------------------
    // Qt apps are multi-threaded (file model, git, LSP, ...) and glibc gives every thread its own arena,
    // which fragments badly and is never returned to the OS. Two arenas are plenty for an IDE, and
    // large blocks (documents, images) go straight to mmap so they are released when freed.
#if defined(__GLIBC__)
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_MMAP_THRESHOLD, 256 * 1024);
    mallopt(M_TRIM_THRESHOLD, 512 * 1024);
#endif

    // GPU compositing: Qt Widgets can composite the window through QRhi (Direct3D 11/12, OpenGL, Vulkan or
    // Metal). It is OFF by default because the GPU driver stack alone costs ~100 MB of RAM and a text editor
    // gains nothing visible from it; enable it in Settings > General, or with --gpu / --gpu-backend=<name>.
    // Must be decided before QApplication.
    QCoreApplication::setOrganizationName(QStringLiteral("CrucibleCode"));
    QCoreApplication::setApplicationName(QStringLiteral("CrucibleCode"));
    bool noGpu = false, gpu = QSettings().value(QStringLiteral("gpu/enabled"), false).toBool();
    QByteArray backend;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--no-gpu") noGpu = true;
        else if (a == "--gpu") gpu = true;
        else if (a.startsWith("--gpu-backend=")) { backend = a.mid(int(sizeof("--gpu-backend=")) - 1); gpu = true; }
    }
    if (gpu && !noGpu) {
        if (!qEnvironmentVariableIsSet("QT_WIDGETS_RHI")) qputenv("QT_WIDGETS_RHI", "1");
        if (!backend.isEmpty()) qputenv("QT_WIDGETS_RHI_BACKEND", backend);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("CrucibleCode"));
    QApplication::setOrganizationName(QStringLiteral("CrucibleCode"));
    QApplication::setApplicationVersion(QStringLiteral(CRUCIBLE_VERSION));
    app.setStyle(QStringLiteral("Fusion"));
    app.setWindowIcon(makeAppIcon());

    QFont uiFont = app.font();
    uiFont.setPointSizeF(9.0);
#ifdef Q_OS_WIN
    uiFont.setFamily(QStringLiteral("Segoe UI"));
#endif
    app.setFont(uiFont);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("CrucibleCode - a lightweight cross-platform IDE for C, C++ and C#"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("gpu"), QStringLiteral("Enable GPU (RHI) window compositing (uses more memory).")});
    parser.addOption({QStringLiteral("no-gpu"), QStringLiteral("Force GPU (RHI) window compositing off (the default).")});
    parser.addOption({QStringLiteral("gpu-backend"),
                      QStringLiteral("GPU backend: opengl, vulkan, d3d11, d3d12 or metal."),
                      QStringLiteral("name")});
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Folder or files to open."), QStringLiteral("[paths...]"));
    parser.process(app);

    QSettings settings;
    ThemeManager::instance().applyKey(settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString());

    MainWindow w;
    w.show();

    // Return freed heap pages to the OS: shortly after startup (file loading, spell/highlight set-up),
    // whenever the window loses focus, and every minute while idle in the background.
    trimMemorySoon(3000);
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app, [](Qt::ApplicationState st) {
        if (st != Qt::ApplicationActive) trimMemorySoon(500);
    });
    auto* trimTimer = new QTimer(&app);
    trimTimer->setInterval(60000);
    QObject::connect(trimTimer, &QTimer::timeout, &app, [] { trimMemoryNow(); });
    trimTimer->start();

    const QStringList paths = parser.positionalArguments();
    if (paths.isEmpty()) {
        w.restoreLastSession();
    } else {
        // Folders first (opening a folder replaces the open tabs), then individual files.
        for (const QString& p : paths)
            if (QFileInfo(p).isDir()) w.openFolder(QFileInfo(p).absoluteFilePath());
        for (const QString& p : paths)
            if (!QFileInfo(p).isDir()) w.openFile(QFileInfo(p).absoluteFilePath());
    }
    return app.exec();
}
