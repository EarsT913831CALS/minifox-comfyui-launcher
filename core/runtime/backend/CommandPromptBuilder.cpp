#include "CommandPromptBuilder.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace {

void prependPathEntry(QStringList &entries, const QString &entry)
{
    if (entry.isEmpty()) {
        return;
    }
    for (const QString &existing : std::as_const(entries)) {
        if (existing.compare(entry, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    entries.append(entry);
}

} // namespace

CommandPromptBuilder::Result CommandPromptBuilder::build(
    const QString &pythonPath,
    const QString &workingDirectory,
    QProcessEnvironment environment)
{
    Result result;
    const QFileInfo python(QDir::cleanPath(pythonPath));
    const QDir pythonDirectory(python.absolutePath());

    QString environmentRoot = pythonDirectory.absolutePath();
    if (pythonDirectory.dirName().compare(QStringLiteral("Scripts"), Qt::CaseInsensitive) == 0) {
        QDir parent = pythonDirectory;
        if (parent.cdUp()) {
            environmentRoot = parent.absolutePath();
        }
    }

    const QStringList activationCandidates {
        pythonDirectory.filePath(QStringLiteral("activate.bat")),
        QDir(environmentRoot).filePath(QStringLiteral("Scripts/activate.bat")),
        QDir(environmentRoot).filePath(QStringLiteral("condabin/activate.bat"))
    };
    for (const QString &candidate : activationCandidates) {
        if (QFileInfo(candidate).isFile()) {
            result.activationScript = QDir::cleanPath(candidate);
            break;
        }
    }

    QStringList pathEntries;
    prependPathEntry(pathEntries, QDir::toNativeSeparators(pythonDirectory.absolutePath()));
    const QString scriptsDirectory = QDir(environmentRoot).filePath(QStringLiteral("Scripts"));
    if (QDir(scriptsDirectory).exists()) {
        prependPathEntry(pathEntries, QDir::toNativeSeparators(scriptsDirectory));
    }
    const QString existingPath = environment.value(QStringLiteral("PATH"));
    if (!existingPath.isEmpty()) {
        pathEntries.append(existingPath);
    }

    environment.insert(QStringLiteral("PATH"), pathEntries.join(QDir::listSeparator()));
    environment.insert(QStringLiteral("VIRTUAL_ENV"), QDir::toNativeSeparators(environmentRoot));
    environment.insert(QStringLiteral("MINIFOX_PYTHON"), python.absoluteFilePath());
    environment.remove(QStringLiteral("PYTHONHOME"));

    // cmd expands %NAME% even inside quotes, so legal paths such as
    // "a-%TEMP%-b" must never be spliced into the command line.  The activation
    // script is called relative to the process working directory, and the target
    // directory arrives through a variable whose value is expanded only once.
    environment.insert(QStringLiteral("MINIFOX_PROMPT_DIRECTORY"),
                       QDir::toNativeSeparators(QDir::cleanPath(workingDirectory)));
    QString activationCommand;
    if (!result.activationScript.isEmpty()) {
        activationCommand = QStringLiteral("call \".\\%1\"")
                                .arg(QFileInfo(result.activationScript).fileName());
        result.workingDirectory = QFileInfo(result.activationScript).absolutePath();
    } else {
        activationCommand = QStringLiteral("prompt (ComfyUI) $P$G");
        result.workingDirectory = QDir::cleanPath(workingDirectory);
    }
    const QString commandLine = QStringLiteral(
        "chcp 65001 >nul && title Minifox ComfyUI Environment & %1 && cd /d \"%MINIFOX_PROMPT_DIRECTORY%\"")
                                    .arg(activationCommand);

    result.program = QStringLiteral("cmd.exe");
    result.nativeArguments = QStringLiteral("/D /K ") + commandLine;
    result.environment = environment;
    result.environmentRoot = QDir::cleanPath(environmentRoot);
    return result;
}
