#ifndef USMENGINEID_H
#define USMENGINEID_H

#include <QByteArray>
#include <QObject>
#include <QString>

// SnmpEngineID helpers for the optional "Device engine ID" of a saved SNMPv3 credential.
// RFC 3411 defines an SnmpEngineID as 5 to 32 octets. Users type or paste it as hex, in any of the
// forms engineers meet in practice: "800000090300AABBCCDDEEFF", "0x8000...", "80:00:00:09:...",
// "80 00 00 09 ..." or with dashes. The canonical stored/display form is lower-case hex without
// separators, so two spellings of the same ID compare equal.
namespace UsmEngineId {

inline constexpr int MinimumBytes = 5;
inline constexpr int MaximumBytes = 32;

// Empty input is valid and means "no engine ID" (empty result). Returns false with a plain-language
// reason in *error when the text is not a usable engine ID.
inline bool parse(const QString &text, QByteArray *bytes, QString *error = nullptr)
{
    if (bytes) bytes->clear();
    QString work = text.trimmed();
    if (work.isEmpty()) return true;
    if (work.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) work.remove(0, 2);
    QString digits;
    for (const QChar c : work) {
        if (c == QLatin1Char(':') || c == QLatin1Char('-') || c == QLatin1Char(' ') || c == QLatin1Char('.')) continue;
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                         (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
                         (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) {
            if (error) *error = QObject::tr("An engine ID uses only the hexadecimal digits 0-9 and A-F.");
            return false;
        }
        digits.append(c);
    }
    if (digits.size() % 2 != 0) {
        if (error) *error = QObject::tr("An engine ID needs an even number of hexadecimal digits.");
        return false;
    }
    const int size = digits.size() / 2;
    if (size < MinimumBytes || size > MaximumBytes) {
        if (error) *error = QObject::tr("An engine ID is %1 to %2 bytes (%3 to %4 hexadecimal digits).")
                                .arg(MinimumBytes).arg(MaximumBytes).arg(MinimumBytes * 2).arg(MaximumBytes * 2);
        return false;
    }
    if (bytes) *bytes = QByteArray::fromHex(digits.toLatin1());
    return true;
}

inline bool isValid(const QByteArray &bytes)
{
    return bytes.isEmpty() || (bytes.size() >= MinimumBytes && bytes.size() <= MaximumBytes);
}

inline QString format(const QByteArray &bytes) { return QString::fromLatin1(bytes.toHex()); }

} // namespace UsmEngineId

#endif
