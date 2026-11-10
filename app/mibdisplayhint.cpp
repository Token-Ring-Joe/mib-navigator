#include "mibdisplayhint.h"

#include <QObject>
#include <QStringDecoder>
#include <QVector>

namespace {
struct OctetFormat { int length; QChar format, separator, terminator; bool repeat; };
bool digit(QChar c) { return c >= u'0' && c <= u'9'; }
bool delimiter(QChar c) { return !digit(c) && c != u'*'; }

// Bound vendor-supplied metadata and reject unsupported numeric widths before
// consuming any value, so fallback cannot expose a partially interpreted value.
bool parse(const QString &hint, QVector<OctetFormat> *formats)
{
    if (hint.isEmpty() || hint.size() > 512) return false;
    qsizetype i = 0;
    while (i < hint.size()) {
        OctetFormat f{};
        if (hint[i] == u'*') { f.repeat = true; ++i; }
        const qsizetype start = i;
        int length = 0;
        while (i < hint.size() && digit(hint[i])) {
            length = length * 10 + hint[i++].unicode() - '0';
            if (length > 65535) return false;
        }
        if (i == start || length == 0 || i == hint.size()) return false;
        f.length = length; f.format = hint[i++];
        if (!QStringLiteral("xdoat").contains(f.format)) return false;
        if (f.format != u'a' && f.format != u't' && length > 8) return false;
        if (i < hint.size() && delimiter(hint[i])) f.separator = hint[i++];
        if (f.repeat && i < hint.size() && delimiter(hint[i])) f.terminator = hint[i++];
        formats->append(f);
        if (formats->size() > 64) return false;
    }
    return !formats->isEmpty();
}
}

MibDisplayHintResult FormatMibOctetHint(const QString &hint, const QByteArray &bytes)
{
    MibDisplayHintResult result;
    QVector<OctetFormat> formats;
    if (bytes.size() > 65535 || !parse(hint, &formats)) return result;
    result.applied = true;
    qsizetype offset = 0, spec = 0;
    bool nonAscii = false, invalidUtf8 = false;
    QString pendingPunctuation; // Only publish punctuation when actual display text follows it.
    while (offset < bytes.size()) {
        const auto &f = formats.at(spec);
        const int repeats = f.repeat ? static_cast<unsigned char>(bytes[offset++]) : 1;
        for (int repetition = 0; repetition < repeats && offset < bytes.size(); ++repetition) {
            const auto part = bytes.mid(offset, qMin(qsizetype(f.length), bytes.size() - offset));
            offset += part.size();
            QString fragment;
            if (f.format == u'a') {
                for (unsigned char c : part) nonAscii |= c > 127;
                fragment = QString::fromLatin1(part);
            } else if (f.format == u't') {
                // An incomplete character at this specification's boundary is
                // discarded, as required by RFC 2579; malformed sequences warn.
                QStringDecoder decoder(QStringDecoder::Utf8);
                fragment = decoder.decode(part);
                invalidUtf8 |= decoder.hasError();
            } else {
                quint64 number = 0;
                for (unsigned char c : part) number = (number << 8) | c;
                const int base = f.format == u'x' ? 16 : f.format == u'o' ? 8 : 10;
                QString text = QString::number(number, base);
                if (base == 16) text = text.rightJustified(int(part.size() * 2), u'0');
                fragment = text;
            }
            if (!fragment.isEmpty()) {
                result.text += pendingPunctuation;
                pendingPunctuation.clear();
                result.text += fragment;
            }
            if (offset < bytes.size() && !f.separator.isNull() &&
                (!f.repeat || repetition + 1 < repeats || f.terminator.isNull()))
                pendingPunctuation += f.separator;
        }
        if (f.repeat && offset < bytes.size() && !f.terminator.isNull()) pendingPunctuation += f.terminator;
        if (spec + 1 < formats.size()) ++spec; // Repeat the last specification for remaining octets.
    }
    if (nonAscii)
        result.warnings << QObject::tr("DISPLAY-HINT expects ASCII, but the received value contains non-ASCII octets.");
    if (invalidUtf8)
        result.warnings << QObject::tr("DISPLAY-HINT expects UTF-8, but the received value contains an invalid UTF-8 sequence.");
    return result;
}

std::optional<QString> FormatMibIntegerHint(const QString &hint, qint64 value)
{
    if (hint.isEmpty() || hint.size() > 8) return {};
    const QChar format = hint[0];
    if (!QStringLiteral("xdob").contains(format)) return {};
    int scale = 0;
    if (hint.size() > 1) {
        if (format != u'd' || hint.size() < 3 || hint[1] != u'-') return {};
        for (qsizetype i = 2; i < hint.size(); ++i) {
            if (!digit(hint[i])) return {};
            scale = scale * 10 + hint[i].unicode() - '0';
            if (scale > 64) return {};
        }
    }
    const quint64 magnitude = value < 0 ? quint64(-(value + 1)) + 1 : quint64(value);
    const int base = format == u'x' ? 16 : format == u'o' ? 8 : format == u'b' ? 2 : 10;
    QString text = QString::number(magnitude, base);
    if (scale) {
        text = text.rightJustified(scale + 1, u'0');
        text.insert(text.size() - scale, u'.');
    }
    if (value < 0) text.prepend(u'-');
    return text;
}
