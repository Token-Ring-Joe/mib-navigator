#ifndef MIBDISPLAYHINT_H
#define MIBDISPLAYHINT_H

#include <QByteArray>
#include <QStringList>
#include <optional>

struct MibDisplayHintResult
{
    bool applied = false;
    QString text;
    QStringList warnings;
};

// RFC 2579 presentation only: unsupported hints retain the received value.
MibDisplayHintResult FormatMibOctetHint(const QString &hint, const QByteArray &bytes);
std::optional<QString> FormatMibIntegerHint(const QString &hint, qint64 value);

#endif
