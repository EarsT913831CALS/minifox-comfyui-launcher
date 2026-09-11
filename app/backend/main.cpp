#include "AppContext.h"
#include "ConfigurationPackageManager.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Qt Quick defaults to D3D11 on Windows. Its flip-model swap chain can
    // stutter badly during live resize on some drivers, including integrated
    // AMD GPUs. The OpenGL RHI backend keeps the native proportional resize
    // responsive without changing the QML scene.
    qputenv("QSG_RHI_BACKEND", "opengl");
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("Minifox"));
    QCoreApplication::setApplicationName(QStringLiteral("Minifox ComfyUI Launcher"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

    QGuiApplication application(argc, argv);
    QString recoveryError;
    if (!ConfigurationPackageManager::recoverPendingState(&recoveryError)) {
#ifdef Q_OS_WIN
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(recoveryError.utf16()),
                    L"Minifox — 配置恢复未完成", MB_OK | MB_ICONERROR);
#else
        qCritical().noquote() << recoveryError;
#endif
        return EXIT_FAILURE;
    }
    AppContext appContext;
    QObject::connect(&application, &QCoreApplication::aboutToQuit,
                     &appContext, [&appContext] {
        appContext.configuration()->savePendingChanges();
    });


    QQmlApplicationEngine engine;
    appContext.setQmlEngine(&engine);
    engine.setInitialProperties({
        {QStringLiteral("appContext"), QVariant::fromValue(static_cast<QObject *>(&appContext))}
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("Minifox.App"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    return application.exec();
}
