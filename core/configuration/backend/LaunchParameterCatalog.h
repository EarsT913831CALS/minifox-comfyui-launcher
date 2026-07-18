#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

struct LaunchParameterDefinition
{
    QString key;
    QString category;
    QString title;
    QString description;
    QString control;
    QString mode;
    QString flag;
    QString disableFlag;
    QVariant defaultValue;
    QVariantList options;
    QVariant minimum;
    QVariant maximum;

    QVariantMap toVariant(const QVariant &currentValue) const;
};

class LaunchParameterCatalog final
{
    Q_DECLARE_TR_FUNCTIONS(LaunchParameterCatalog)

public:
    static const QList<LaunchParameterDefinition> &parameters();
    static QVariantList categories();
    static const LaunchParameterDefinition *find(const QString &key);
};
