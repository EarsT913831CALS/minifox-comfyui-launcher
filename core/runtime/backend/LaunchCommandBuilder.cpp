#include "LaunchCommandBuilder.h"

#include "ConfigurationManager.h"
#include "LaunchParameterCatalog.h"

#include <QProcess>
#include <QRegularExpression>

namespace {

QVariant valueFor(const LaunchParameterDefinition &definition, const QVariantMap &values)
{
    return values.contains(definition.key) ? values.value(definition.key) : definition.defaultValue;
}

QStringList nonEmptyLines(const QString &text)
{
    QStringList result;
    for (const QString &line : text.split(QRegularExpression(QStringLiteral("[\r\n]+")))) {
        if (!line.trimmed().isEmpty()) {
            result.append(line.trimmed());
        }
    }
    return result;
}

QString optionArgument(const LaunchParameterDefinition &definition, const QString &value)
{
    for (const auto &entry : definition.options) {
        const QVariantMap option = entry.toMap();
        if (option.value(QStringLiteral("value")).toString() == value) {
            return option.value(QStringLiteral("argument")).toString();
        }
    }
    return {};
}

} // namespace

LaunchCommandBuilder::Result LaunchCommandBuilder::build(const QVariantMap &profile)
{
    Result result;
    result.program = profile.value(QStringLiteral("pythonPath")).toString();
    result.workingDirectory = profile.value(QStringLiteral("comfyRoot")).toString();
    result.arguments = {QStringLiteral("main.py")};
    result.arguments.append(parameterArguments(profile.value(QStringLiteral("parameters")).toMap()));

    const QString customArguments = profile.value(QStringLiteral("customArguments")).toString().trimmed();
    if (!customArguments.isEmpty()) {
        result.arguments.append(QProcess::splitCommand(customArguments));
    }

    result.environment = QProcessEnvironment::systemEnvironment();
    result.environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    result.environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    result.environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    QStringList environmentPreview {
        QStringLiteral("set \"PYTHONUTF8=1\""),
        QStringLiteral("set \"PYTHONIOENCODING=utf-8\""),
        QStringLiteral("set \"PYTHONUNBUFFERED=1\"")
    };
    static const QRegularExpression environmentName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (const auto &value : profile.value(QStringLiteral("environment")).toList()) {
        const QVariantMap entry = value.toMap();
        if (!entry.value(QStringLiteral("enabled"), true).toBool()) {
            continue;
        }
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        const QString environmentValue = entry.value(QStringLiteral("value")).toString();
        if (!environmentName.match(name).hasMatch()) {
            continue;
        }
        result.environment.insert(name, environmentValue);
        environmentPreview.append(QStringLiteral("set \"%1=%2\"")
                                      .arg(name, maskedEnvironmentValue(name, environmentValue)));
    }

    QStringList commandParts;
    commandParts.append(QStringLiteral("cd /d %1").arg(quoteWindowsArgument(result.workingDirectory)));
    commandParts.append(environmentPreview);

    QStringList executableParts {quoteWindowsArgument(result.program)};
    for (const QString &argument : result.arguments) {
        executableParts.append(quoteWindowsArgument(argument));
    }
    commandParts.append(executableParts.join(QLatin1Char(' ')));
    result.preview = commandParts.join(QStringLiteral(" && "));
    return result;
}

QString LaunchCommandBuilder::quoteWindowsArgument(const QString &argument)
{
    if (argument.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    if (!argument.contains(QRegularExpression(QStringLiteral("[\\s\"]")))) {
        return argument;
    }

    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar character : argument) {
        if (character == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (character == QLatin1Char('"')) {
            quoted.append(QString(backslashes * 2 + 1, QLatin1Char('\\')));
            quoted.append(character);
            backslashes = 0;
            continue;
        }
        quoted.append(QString(backslashes, QLatin1Char('\\')));
        backslashes = 0;
        quoted.append(character);
    }
    quoted.append(QString(backslashes * 2, QLatin1Char('\\')));
    quoted.append(QLatin1Char('"'));
    return quoted;
}

QStringList LaunchCommandBuilder::parameterArguments(const QVariantMap &parameters)
{
    QStringList arguments;
    for (const auto &definition : LaunchParameterCatalog::parameters()) {
        const QVariant value = valueFor(definition, parameters);

        if (definition.mode == QStringLiteral("auxiliary")) {
            continue;
        }
        if (definition.mode == QStringLiteral("switch")) {
            if (value.toBool()) {
                arguments.append(definition.flag);
            }
            continue;
        }
        if (definition.mode == QStringLiteral("flagChoice")
            || definition.mode == QStringLiteral("triState")) {
            const QString argument = optionArgument(definition, value.toString());
            if (!argument.isEmpty()) {
                arguments.append(argument);
            }
            continue;
        }
        if (definition.mode == QStringLiteral("value")) {
            if (!value.isValid() || value.toString().trimmed().isEmpty() || value == definition.defaultValue) {
                continue;
            }
            arguments.append(definition.flag);
            arguments.append(value.toString());
            continue;
        }
        if (definition.mode == QStringLiteral("repeat")) {
            for (const QString &entry : nonEmptyLines(value.toString())) {
                arguments.append(definition.flag);
                arguments.append(entry);
            }
            continue;
        }
        if (definition.mode == QStringLiteral("list")) {
            const QStringList entries = nonEmptyLines(value.toString());
            if (!entries.isEmpty()) {
                arguments.append(definition.flag);
                arguments.append(entries);
            }
            continue;
        }
        if (definition.mode != QStringLiteral("special")) {
            continue;
        }

        if (definition.key == QStringLiteral("directml")) {
            const QString directml = value.toString().trimmed();
            if (!directml.isEmpty()) {
                arguments.append(QStringLiteral("--directml"));
                if (directml.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
                    arguments.append(directml);
                }
            }
        } else if (definition.key == QStringLiteral("cacheMode")) {
            const QString cacheMode = value.toString();
            const QString cacheValue = parameters.value(QStringLiteral("cacheValue")).toString().trimmed();
            if (cacheMode == QStringLiteral("ram")) {
                arguments.append(QStringLiteral("--cache-ram"));
                arguments.append(QProcess::splitCommand(cacheValue));
            } else if (cacheMode == QStringLiteral("lru")) {
                arguments.append({QStringLiteral("--cache-lru"), cacheValue.isEmpty() ? QStringLiteral("0") : cacheValue});
            } else if (!cacheMode.isEmpty()) {
                const QString mapped = optionArgument(definition, cacheMode);
                if (!mapped.isEmpty()) {
                    arguments.append(mapped);
                }
            }
        } else if (definition.key == QStringLiteral("asyncOffload")) {
            if (value.toString() == QStringLiteral("enable")) {
                arguments.append(QStringLiteral("--async-offload"));
                const QString streams = parameters.value(QStringLiteral("asyncOffloadStreams")).toString().trimmed();
                if (!streams.isEmpty()) {
                    arguments.append(streams);
                }
            } else if (value.toString() == QStringLiteral("disable")) {
                arguments.append(QStringLiteral("--disable-async-offload"));
            }
        } else if (definition.key == QStringLiteral("fastFeatures")) {
            const QString features = value.toString().trimmed();
            if (!features.isEmpty()) {
                arguments.append(QStringLiteral("--fast"));
                if (features.compare(QStringLiteral("all"), Qt::CaseInsensitive) != 0) {
                    arguments.append(QProcess::splitCommand(features));
                }
            }
        }
    }
    return arguments;
}

QString LaunchCommandBuilder::maskedEnvironmentValue(const QString &name, const QString &value)
{
    return ConfigurationManager::isSensitiveEnvironmentName(name) && !value.isEmpty()
        ? QStringLiteral("••••••••")
        : value;
}
