#pragma once

#include "ApplicationIconManager.h"
#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "HardwareManager.h"
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
    Q_PROPERTY(HardwareManager *hardware READ hardware CONSTANT)
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
    HardwareManager *hardware() const;
    RuntimeManager *runtime() const;
    ApplicationSettings *settings() const;
    SkinManager *skins() const;
    WindowChromeController *windowChrome() const;
    VersionManager *versions() const;
    void setQmlEngine(QQmlEngine *engine);

private:
    void applyLanguage();

    ConfigurationManager *m_configuration;
    ApplicationSettings *m_settings;
    ApplicationIconManager *m_appIcon;
    HardwareManager *m_hardware;
    RuntimeManager *m_runtime;
    SkinManager *m_skins;
    WindowChromeController *m_windowChrome;
    VersionManager *m_versions;
    QPointer<QThread> m_zludaPreloadThread;
    QQmlEngine *m_qmlEngine = nullptr;
    QTranslator m_translator;
};
