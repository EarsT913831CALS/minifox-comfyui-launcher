#include "ProcessTextDecoder.h"

#include <QStringConverter>

namespace ProcessTextDecoder {

QString decode(const QByteArray &data)
{
    if (data.isEmpty()) {
        return {};
    }

    QByteArray payload = data;
    if (payload.startsWith("\xEF\xBB\xBF")) {
        payload.remove(0, 3);
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString utf8 = decoder(payload);
    return decoder.hasError() ? QString::fromLocal8Bit(payload) : utf8;
}

} // namespace ProcessTextDecoder
