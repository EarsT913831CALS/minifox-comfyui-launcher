#pragma once

#include <QString>

class PortablePaths final
{
public:
    static QString dataDirectory();
    static QString configurationFile();
    static QString settingsFile();
    static bool ensureDataDirectory(QString *errorMessage = nullptr);
};
