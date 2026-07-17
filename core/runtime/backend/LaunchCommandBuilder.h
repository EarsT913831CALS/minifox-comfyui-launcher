#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class LaunchCommandBuilder final
{
public:
    struct Result {
        QString program;
        QString workingDirectory;
        QStringList arguments;
        QProcessEnvironment environment;
        QString preview;
    };

    static Result build(const QVariantMap &profile);
    static QString quoteWindowsArgument(const QString &argument);

private:
    static QStringList parameterArguments(const QVariantMap &parameters);
    static QString maskedEnvironmentValue(const QString &name, const QString &value);
};
