#include "AppContext.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("Minifox"));
    QCoreApplication::setApplicationName(QStringLiteral("Minifox ComfyUI Launcher"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));

    QGuiApplication application(argc, argv);
    AppContext appContext;

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
