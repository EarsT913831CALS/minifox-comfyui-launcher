#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class CommandPromptBuilder final
{
public:
    struct Result {
        QString program;
        QString nativeArguments;
        QString workingDirectory;
        QProcessEnvironment environment;
        QString activationScript;
        QString environmentRoot;
    };

    static Result build(const QString &pythonPath,
                        const QString &workingDirectory,
                        QProcessEnvironment environment);
};
