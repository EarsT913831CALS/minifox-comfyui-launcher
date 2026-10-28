#pragma once

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

#include <atomic>

namespace git {

struct Result {
    bool started = false;
    bool timedOut = false;
    bool cancelled = false;
    int exitCode = -1;
    QByteArray out;
    QByteArray err;

    bool ok() const { return started && !timedOut && !cancelled && exitCode == 0; }
    // First line of stderr, or a description of why Git did not finish.
    QString message() const;
    // message(), with common network failures explained in plain words.
    QString networkMessage() const;
    QString text() const;
};

struct Options {
    // Private index for GIT_INDEX_FILE; empty uses the repository index.
    QString index;
    QByteArray input;
    // Killed after this long without any output.
    int idleTimeoutMs = 120000;
    const std::atomic_bool *cancel = nullptr;
    // Commit objects need an identity; never take it from the user's config.
    bool identity = false;
};

// An immutable Git executable plus environment. Safe to copy across threads;
// run() blocks and must only be called on worker threads.
class Tool final
{
public:
    Tool() = default;
    Tool(QString program, QProcessEnvironment environment);

    // PATH first, then the Git shipped next to the launcher or the ComfyUI
    // portable package, then the default Git for Windows locations.
    static QString locate(const QString &comfyRoot);

    bool isValid() const { return !m_program.isEmpty(); }
    QString program() const { return m_program; }

    Result run(const QString &root, const QStringList &arguments, const Options &options = {}) const;
    QVersionNumber version() const;

private:
    QString m_program;
    QProcessEnvironment m_environment;
};

// `git merge-tree --merge-base` exists since Git 2.40.
inline const QVersionNumber kMergeTreeVersion(2, 40);

QStringList lines(const QByteArray &output);
QList<QByteArray> fields(const QByteArray &output, char separator = '\0');
bool isObjectId(const QString &text);

}
