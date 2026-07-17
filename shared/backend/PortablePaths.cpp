#include "PortablePaths.h"

#include <QCoreApplication>
#include <QDir>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

QString PortablePaths::dataDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(".minifox"));
}

QString PortablePaths::configurationFile()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("launch-profiles.json"));
}

QString PortablePaths::settingsFile()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("application-settings.json"));
}

bool PortablePaths::ensureDataDirectory(QString *errorMessage)
{
    const QString path = dataDirectory();
    if (!QDir().mkpath(path)) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate(
                "PortablePaths",
                "Unable to create the portable data directory: %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

#ifdef Q_OS_WIN
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(nativePath.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
    }
#endif

    return true;
}
