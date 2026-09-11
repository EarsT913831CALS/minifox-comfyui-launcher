#include <QCoreApplication>
#include "ExternalContent.h"
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>

bool ExternalContent::allowedUrl(const QUrl &url) {
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (url.scheme() != "https" && url.scheme() != "http")) return false;
    const QString decoded = QUrl::fromPercentEncoding(url.toEncoded());
    for (const QChar c : decoded) if (c.unicode() < 32 || c.unicode() == 127 || c == '\\') return false;
    return true;
}

QUrl ExternalContent::repositoryUrl(const QString &remote) {
    for (const QChar c : remote) if (c.unicode() < 32 || c.unicode() == 127 || c == '\\') return {};
    QString value = remote.trimmed();
    static const QRegularExpression scp("^git@([^/:@]+):(.+)$");
    const auto match = scp.match(value);
    if (match.hasMatch()) value = "https://" + match.captured(1) + '/' + match.captured(2);
    QUrl url(value, QUrl::StrictMode);
    if (url.scheme() == "ssh" && url.userName() == "git" && url.password().isEmpty()) {
        url.setScheme("https"); url.setUserName({}); url.setPort(-1);
    }
    if (!allowedUrl(url)) return {};
    if (url.path().endsWith(".git")) url.setPath(url.path().chopped(4));
    return url;
}

QString ExternalContent::markdown(const QString &source) {
    class NoResources final : public QTextDocument {
        QVariant loadResource(int, const QUrl &) override { return {}; }
    } document;
    document.setMarkdown(source);
    struct Edit { int position; int length; bool image; QTextCharFormat format; };
    QList<Edit> edits;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid()) continue;
            auto format = fragment.charFormat();
            if (format.isImageFormat()) edits.append({fragment.position(), fragment.length(), true, {}});
            else if (format.isAnchor() && !allowedUrl(QUrl(format.anchorHref(), QUrl::StrictMode))) {
                format.setAnchor(false); format.setAnchorHref({});
                edits.append({fragment.position(), fragment.length(), false, format});
            }
        }
    }
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) {
        QTextCursor cursor(&document); cursor.setPosition(it->position);
        cursor.setPosition(it->position + it->length, QTextCursor::KeepAnchor);
        if (it->image) cursor.removeSelectedText(); else cursor.setCharFormat(it->format);
    }
    return document.toHtml();
}

QString ExternalContent::redactUrl(const QString &source) {
    QUrl url(source, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().isEmpty()) return QCoreApplication::translate("ExternalContent", "[已隐藏]");
    url.setUserInfo({}); url.setQuery({}); url.setFragment({});
    return url.toString();
}
