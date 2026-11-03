#ifndef CSVFORMAT_H
#define CSVFORMAT_H

#include <QList>
#include <QString>
#include <QStringList>

// RFC 4180 comma-separated text: fields containing a comma, quote, or line break are quoted
// with embedded quotes doubled, and every record (including the last) ends with CRLF so that
// Excel, LibreOffice and scripts all read it the same way.
inline QString FormatCsv(const QList<QStringList> &records)
{
    QString text;
    for (const QStringList &record : records) {
        for (qsizetype i = 0; i < record.size(); ++i) {
            if (i) text += QLatin1Char(',');
            const QString &field = record.at(i);
            if (field.contains(QLatin1Char(',')) || field.contains(QLatin1Char('"')) ||
                field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'))) {
                QString quoted = field;
                quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                text += QLatin1Char('"') + quoted + QLatin1Char('"');
            } else {
                text += field;
            }
        }
        text += QStringLiteral("\r\n");
    }
    return text;
}

#endif
