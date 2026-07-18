#pragma once

#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "RuntimeManager.h"

#include <QObject>
#include <QTranslator>

class QQmlEngine;

class AppContext final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ConfigurationManager *configuration READ configuration CONSTANT)
    Q_PROPERTY(RuntimeManager *runtime READ runtime CONSTANT)
    Q_PROPERTY(ApplicationSettings *settings READ settings CONSTANT)

public:
    explicit AppContext(QObject *parent = nullptr);

    ConfigurationManager *configuration() const;
    RuntimeManager *runtime() const;
    ApplicationSettings *settings() const;
    void setQmlEngine(QQmlEngine *engine);

private:
    void applyLanguage();

    ConfigurationManager *m_configuration;
    ApplicationSettings *m_settings;
    RuntimeManager *m_runtime;
    QQmlEngine *m_qmlEngine = nullptr;
    QTranslator m_translator;
};
