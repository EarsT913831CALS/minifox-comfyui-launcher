#pragma once

#include "ApplicationIconManager.h"
#include "ApplicationSettings.h"
#include "ConfigurationPackageManager.h"
#include "ConfigurationManager.h"
#include "RuntimeManager.h"
#include "SkinManager.h"
#include "VersionManager.h"
#include "WindowChromeController.h"

#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTranslator>

class QQmlEngine;

class AppContext final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ApplicationIconManager *appIcon READ appIcon CONSTANT)
    Q_PROPERTY(ConfigurationManager *configuration READ configuration CONSTANT)
    Q_PROPERTY(ConfigurationPackageManager *configurationPackages READ configurationPackages CONSTANT)
    Q_PROPERTY(RuntimeManager *runtime READ runtime CONSTANT)
    Q_PROPERTY(ApplicationSettings *settings READ settings CONSTANT)
    Q_PROPERTY(SkinManager *skins READ skins CONSTANT)
    Q_PROPERTY(WindowChromeController *windowChrome READ windowChrome CONSTANT)
    Q_PROPERTY(VersionManager *versions READ versions CONSTANT)

public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    ApplicationIconManager *appIcon() const;
    ConfigurationManager *configuration() const;
    ConfigurationPackageManager *configurationPackages() const;
    RuntimeManager *runtime() const;
    ApplicationSettings *settings() const;
    SkinManager *skins() const;
    WindowChromeController *windowChrome() const;
    VersionManager *versions() const;
    void setQmlEngine(QQmlEngine *engine);

private:
    void applyLanguage();
    void installLanguage(const QString &preference);

    ConfigurationManager *m_configuration = nullptr;
    ConfigurationPackageManager *m_configurationPackages = nullptr;
    ApplicationSettings *m_settings = nullptr;
    ApplicationIconManager *m_appIcon = nullptr;
    RuntimeManager *m_runtime = nullptr;
    SkinManager *m_skins = nullptr;
    WindowChromeController *m_windowChrome = nullptr;
    VersionManager *m_versions = nullptr;
    QPointer<QThread> m_zludaPreloadThread;
    QQmlEngine *m_qmlEngine = nullptr;
    QTranslator m_translator;
};
