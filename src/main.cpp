#include "MainWindow.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QSettings>

int main(int argc, char* argv[])
{
    // GPU compositing: Qt Widgets can composite the window through QRhi
    // (Direct3D 11/12, OpenGL, Vulkan or Metal). Must be decided before QApplication.
    bool noGpu = false;
    QByteArray backend;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--no-gpu") noGpu = true;
        else if (a.startsWith("--gpu-backend=")) backend = a.mid(int(sizeof("--gpu-backend=")) - 1);
    }
    if (!noGpu) {
        if (!qEnvironmentVariableIsSet("QT_WIDGETS_RHI")) qputenv("QT_WIDGETS_RHI", "1");
        if (!backend.isEmpty()) qputenv("QT_WIDGETS_RHI_BACKEND", backend);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Forge"));
    QApplication::setOrganizationName(QStringLiteral("ForgeIDE"));
    QApplication::setApplicationVersion(QStringLiteral(FORGE_VERSION));
    app.setStyle(QStringLiteral("Fusion"));

    QFont uiFont = app.font();
    uiFont.setPointSizeF(9.0);
#ifdef Q_OS_WIN
    uiFont.setFamily(QStringLiteral("Segoe UI"));
#endif
    app.setFont(uiFont);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Forge - a lightweight cross-platform C++ IDE"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("no-gpu"), QStringLiteral("Disable GPU (RHI) window compositing.")});
    parser.addOption({QStringLiteral("gpu-backend"),
                      QStringLiteral("GPU backend: opengl, vulkan, d3d11, d3d12 or metal."),
                      QStringLiteral("name")});
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Folder or files to open."), QStringLiteral("[paths...]"));
    parser.process(app);

    QSettings settings;
    ThemeManager::instance().apply(
        ThemeManager::nameToId(settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString()));

    MainWindow w;
    w.show();

    const QStringList paths = parser.positionalArguments();
    if (paths.isEmpty()) {
        w.restoreLastSession();
    } else {
        for (const QString& p : paths) {
            const QFileInfo fi(p);
            if (fi.isDir()) w.openFolder(fi.absoluteFilePath());
            else w.openFile(fi.absoluteFilePath());
        }
    }
    return app.exec();
}
