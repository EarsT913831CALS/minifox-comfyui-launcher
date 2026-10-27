#pragma once
#include <QProcess>
#include <QStringList>

namespace DependencyInstaller {
// Offline requirements checker run with `python -c`.  Each positional argument is
// one requirements.txt and gets its own JSON result entry; nothing is installed.
QString checkerScript();

// Runs `python -m pip install -r <file>` and mirrors its output both to the
// launcher pipe and to a visible console window.
QString installerScript();

// Prepares the offline check without a console window.
void configureCheck(QProcess &process, const QString &python, const QStringList &requirements);

// Prepares a real install: visible console, working directory = the folder of the
// requirements file so relative wheel/path entries resolve like a manual pip run.
// Paths remain arguments to Python; no cmd, batch expansion, or marker file.
void configure(QProcess &process, const QString &python, const QString &requirements);
}
