#include "Git.h"

#include "ProcessTextDecoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace git {

QString Result::message() const
{
    if (!started) return QCoreApplication::translate("Git", "无法启动 Git。");
    if (cancelled) return QCoreApplication::translate("Git", "操作已取消。");
    if (timedOut) return QCoreApplication::translate("Git", "Git 长时间没有响应，已停止。");
    const QString error = ProcessTextDecoder::decode(err).trimmed();
    for (const QString &line : error.split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        // Hints and progress are noise; the first real line explains the failure.
        if (trimmed.startsWith(QStringLiteral("hint:")) || trimmed.startsWith(QStringLiteral("warning:")))
            continue;
        return trimmed;
    }
    return QCoreApplication::translate("Git", "Git 退出代码 %1。").arg(exitCode);
}

QString Result::networkMessage() const
{
    if (!started || cancelled || timedOut) return message();
    const QString error = ProcessTextDecoder::decode(err).toLower();
    const auto any = [&](std::initializer_list<const char *> needles) {
        for (const char *needle : needles) {
            if (error.contains(QLatin1String(needle))) return true;
        }
        return false;
    };
    if (any({"could not resolve host", "could not resolve proxy"}))
        return QCoreApplication::translate("Git", "无法解析服务器地址，请检查网络或代理设置。");
    if (any({"failed to connect", "couldn't connect", "connection timed out", "timed out", "connection refused"}))
        return QCoreApplication::translate("Git", "连接服务器失败，请检查网络或代理设置。");
    if (any({"ssl", "certificate", "schannel"}))
        return QCoreApplication::translate("Git", "与服务器的安全连接失败，请检查代理设置或系统时间。");
    if (any({"repository not found", "does not appear to be a git repository", "not found"}))
        return QCoreApplication::translate("Git", "远端仓库不存在，地址可能有误或仓库已被删除。");
    if (any({"authentication failed", "could not read username", "terminal prompts disabled", "permission denied"}))
        return QCoreApplication::translate("Git", "远端仓库需要登录，可能是私有仓库或地址有误。");
    if (any({"early eof", "rpc failed", "unexpected disconnect", "connection reset", "the remote end hung up"}))
        return QCoreApplication::translate("Git", "下载中途断开，请重试。");
    return message();
}

QString Result::text() const
{
    return ProcessTextDecoder::decode(out);
}

Tool::Tool(QString program, QProcessEnvironment environment)
    : m_program(std::move(program)), m_environment(std::move(environment))
{
    m_environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    m_environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    m_environment.insert(QStringLiteral("GIT_ASKPASS"), QString());
    // Stable English messages: they are parsed here and translated for users.
    m_environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_environment.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
    m_environment.remove(QStringLiteral("GIT_DIR"));
    m_environment.remove(QStringLiteral("GIT_WORK_TREE"));
    m_environment.remove(QStringLiteral("GIT_INDEX_FILE"));
}

QString Tool::locate(const QString &comfyRoot)
{
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (!onPath.isEmpty()) return onPath;
    const QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("git/cmd/git.exe")),
        QDir(comfyRoot).filePath(QStringLiteral("../git/cmd/git.exe")),
        QStringLiteral("C:/Program Files/Git/cmd/git.exe"),
        QStringLiteral("C:/Program Files/Git/bin/git.exe"),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isFile()) return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

Result Tool::run(const QString &root, const QStringList &arguments, const Options &options) const
{
    Result result;
    if (!isValid()) return result;

    QStringList processArguments{
        // Portable packages are often copied between drives or users; Git then
        // refuses them as "dubious ownership". Only these processes trust them.
        QStringLiteral("-c"), QStringLiteral("safe.directory=*"),
        QStringLiteral("-c"), QStringLiteral("core.longpaths=true"),
        QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        // Paths come from repositories; "[id].py" must not act as a glob.
        QStringLiteral("--literal-pathspecs"),
    };
    if (!root.isEmpty()) processArguments << QStringLiteral("-C") << root;
    processArguments << arguments;

    QProcessEnvironment environment = m_environment;
    if (!options.index.isEmpty()) environment.insert(QStringLiteral("GIT_INDEX_FILE"), options.index);
    if (options.identity) {
        for (const char *name : {"GIT_AUTHOR", "GIT_COMMITTER"}) {
            environment.insert(QString::fromLatin1(name) + QStringLiteral("_NAME"), QStringLiteral("Minifox"));
            environment.insert(QString::fromLatin1(name) + QStringLiteral("_EMAIL"), QStringLiteral("minifox@localhost"));
        }
    }

    QProcess process;
    process.setProgram(m_program);
    process.setArguments(processArguments);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(15000)) return result;
    result.started = true;

    if (!options.input.isEmpty()) process.write(options.input);
    process.closeWriteChannel();

    QElapsedTimer idle;
    idle.start();
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        const QByteArray out = process.readAllStandardOutput();
        const QByteArray err = process.readAllStandardError();
        if (!out.isEmpty() || !err.isEmpty()) idle.restart();
        result.out += out;
        result.err += err;
        if (options.cancel && options.cancel->load()) {
            result.cancelled = true;
            process.kill();
        } else if (idle.elapsed() > options.idleTimeoutMs) {
            result.timedOut = true;
            process.kill();
        }
    }
    process.waitForFinished(5000);
    result.out += process.readAllStandardOutput();
    result.err += process.readAllStandardError();
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    return result;
}

QVersionNumber Tool::version() const
{
    const Result result = run({}, {QStringLiteral("--version")}, {.idleTimeoutMs = 15000});
    // "git version 2.56.0.windows.3"
    const auto match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)(?:\\.(\\d+))?"))
                           .match(result.text());
    if (!result.ok() || !match.hasMatch()) return {};
    return QVersionNumber(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
}

QStringList lines(const QByteArray &output)
{
    QStringList result;
    for (const QString &line : ProcessTextDecoder::decode(output).split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) result.append(trimmed);
    }
    return result;
}

QList<QByteArray> fields(const QByteArray &output, char separator)
{
    QList<QByteArray> result = output.split(separator);
    if (!result.isEmpty() && result.constLast().isEmpty()) result.removeLast();
    return result;
}

bool isObjectId(const QString &text)
{
    static const QRegularExpression expression(QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
    return expression.match(text).hasMatch();
}

}
