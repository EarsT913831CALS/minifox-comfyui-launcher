#pragma once
#include <QProcess>

namespace DependencyInstaller {
inline void configure(QProcess &process, const QString &python, const QString &requirements)
{
    // Paths remain arguments to Python; no cmd, batch expansion, or marker file.
    process.setProgram(python);
    process.setArguments({QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"),
                          QStringLiteral("-r"), requirements});
#ifdef Q_OS_WIN
    process.setNativeArguments({});
    process.setCreateProcessArgumentsModifier({});
#endif
}
}
