#pragma once

#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "HardwareManager.h"
#include "RuntimeManager.h"
#include "VersionManager.h"

#include <QObject>
#include <QTranslator>

class QQmlEngine;

class AppContext final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ConfigurationManager *configuration READ configuration CONSTANT)
    Q_PROPERTY(HardwareManager *hardware READ hardware CONSTANT)
    Q_PROPERTY(RuntimeManager *runtime READ runtime CONSTANT)
    Q_PROPERTY(ApplicationSettings *settings READ settings CONSTANT)
    Q_PROPERTY(VersionManager *versions READ versions CONSTANT)

public:
    explicit AppContext(QObject *parent = nullptr);

    ConfigurationManager *configuration() const;
    HardwareManager *hardware() const;
    RuntimeManager *runtime() const;
    ApplicationSettings *settings() const;
    VersionManager *versions() const;
    void setQmlEngine(QQmlEngine *engine);

private:
    void applyLanguage();

    ConfigurationManager *m_configuration;
    ApplicationSettings *m_settings;
    HardwareManager *m_hardware;
    RuntimeManager *m_runtime;
    VersionManager *m_versions;
    QQmlEngine *m_qmlEngine = nullptr;
    QTranslator m_translator;
};
