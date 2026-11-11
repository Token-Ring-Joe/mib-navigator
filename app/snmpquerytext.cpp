#include "snmpquerytext.h"

#include "snmpreport.h"

#include <QCoreApplication>

// The strings keep the "Agent" translation context they had when this text was built there.
namespace
{
QString timeoutText()
{
    return QCoreApplication::translate("Agent", "<font color=red>Timeout: no response from the device. Check the Target address and that SNMP is enabled on the device. SNMPv1/v2c devices silently ignore a wrong community, and SNMPv3 devices may ignore unknown users.</font>");
}

QString plainError(int code)
{
    return QString::fromLatin1(Snmp::error_msg(code));
}
}

QString FormatObjectLine(int number, const MibResolvedObject &resolved, const Oid &oid, const Vb &vb)
{
    if (resolved.node)
        return QCoreApplication::translate("Agent", "%1: %2    <font color=blue>%3</font>")
            .arg(number).arg(RenderMibOid(resolved.environment, oid))
            .arg(RenderMibValue(resolved, vb).toHtmlEscaped());
    return QCoreApplication::translate("Agent", "%1: %2    <font color=blue>%3</font>")
        .arg(number).arg(QString::fromLatin1(vb.get_printable_oid()))
        .arg(QString::fromLatin1(vb.get_printable_value()).toHtmlEscaped());
}

namespace
{
SnmpQueryText formatResponse(const SnmpSingleResult &result, const SnmpObjectResolver &resolve,
                             bool stopped, bool set);
}

SnmpQueryText FormatSingleResult(const SnmpSingleResult &result, const SnmpObjectResolver &resolve,
                                 bool stopped)
{
    return formatResponse(result, resolve, stopped, false);
}

SnmpQueryText FormatSetResult(const SnmpSingleResult &result, const SnmpObjectResolver &resolve)
{
    return formatResponse(result, resolve, false, true);
}

namespace
{
SnmpQueryText formatResponse(const SnmpSingleResult &result, const SnmpObjectResolver &resolve,
                             bool stopped, bool set)
{
    const auto errorText = [set](int code) { return set ? ExplainSnmpErrorStatus(code) : plainError(code); };
    SnmpQueryText out;
    QString msg;
    const auto fail = [&out](const QString &text) {
        out.paragraphs << text;
        return out;
    };

    switch (result.status) {
    case SnmpOperationStatus::Success:
    case SnmpOperationStatus::SnmpError:
        break;
    case SnmpOperationStatus::Timeout:
        return fail(result.problem.isEmpty() ? timeoutText()
                                             : QStringLiteral("<font color=red>%1</font>").arg(result.problem.toHtmlEscaped()));
    default:
        return fail(!result.problem.isEmpty()
            ? QStringLiteral("<font color=red>%1</font>").arg(result.problem.toHtmlEscaped())
            : QCoreApplication::translate("Agent", "<font color=red>No response received</font>"));
    }
    // An SNMPv3 REPORT is the device refusing the request; never present it as data.
    if (!result.problem.isEmpty())
        return fail(QStringLiteral("<font color=red>%1</font>").arg(result.problem.toHtmlEscaped()));

    const Pdu &pdu = result.pdu;
    const int pduError = pdu.get_error_status();
    int pduIndex = 0;
    int startIndex = 0;
    int objects = 0;
    if (pduError) {
        pduIndex = pdu.get_error_index();
        if (pduIndex > 0)
            startIndex = objects = pduIndex - 1;
        else
            return fail(QCoreApplication::translate("Agent", "<font color=red>%1</font><br>")
                            .arg(errorText(pduError)));
    }
    if (pdu.get_vb_count() == 0)
        return fail(QCoreApplication::translate("Agent", "<font color=red>Pdu is empty</font>"));

    int requests = 1;
    Vb vb;
    for (int z = startIndex; z < pdu.get_vb_count(); ++z) {
        pdu.get_vb(vb, z);

        // look for var bind exception, applies to v2 only
        int vbError = vb.get_syntax();
        if (vbError == sNMP_SYNTAX_ENDOFMIBVIEW)
            break;
        Oid oid;
        vb.get_oid(oid);
        if (vbError != sNMP_SYNTAX_NOSUCHOBJECT && vbError != sNMP_SYNTAX_NOSUCHINSTANCE)
            vbError = 0;

        ++objects;
        const MibResolvedObject resolved = resolve(oid);
        if (!set)   // a Set has never filled the Results table
            out.rows.append({resolved, oid, vb});
        const bool flagged = vbError || (pduError && z + 1 == pduIndex);
        const QString reason = pduError
            ? errorText(pduError)
            //: These two strings might best be left untranslated. It's like "not found" in a "404 Not Found".
            : vbError == sNMP_SYNTAX_NOSUCHOBJECT ? QCoreApplication::translate("Agent", "No Such Object")
                                                  : QCoreApplication::translate("Agent", "No Such Instance");
        if (resolved.node) {
            if (flagged)
                msg += QCoreApplication::translate("Agent", "<font color=red>ERROR on varbind #</font>");

            // Print the OID part
            msg += QString("%1: %2").arg(objects).arg(RenderMibOid(resolved.environment, oid));

            if (flagged) {
                msg += QCoreApplication::translate("Agent", "<font color=red><br>%1</font><br>").arg(reason);
                break;
            }

            // Print the value part
            msg += QCoreApplication::translate("Agent", "    <font color=blue>%1</font>")
                       .arg(RenderMibValue(resolved, vb).toHtmlEscaped());
        } else {
            if (flagged) {
                msg += QCoreApplication::translate("Agent", "<font color=red>ERROR on varbind #</font>%1: %2")
                           .arg(objects).arg(QString::fromLatin1(vb.get_printable_oid()));
                msg += QCoreApplication::translate("Agent", "<font color=red><br>%3</font><br>").arg(reason);
            } else {
                /* Unknown OID */
                msg += FormatObjectLine(objects, resolved, oid, vb);
            }
        }

        // TextEdit append is too slow ... :-(
        // Buffer each 10 objects before displaying.
        if (!(objects % 10)) {
            out.paragraphs << msg;
            msg.clear();
        } else {
            msg += "<br>";
        }
    }

    if (set) {
        msg += QCoreApplication::translate("Agent", "-----SNMP set finished-----<br>");
        out.paragraphs << msg;
        out.objects = objects;
        out.requests = requests;
        return out;
    }
    msg += stopped ? QCoreApplication::translate("Agent", "<font color=red>-----SNMP query stopped-----</font><br>")
                   : QCoreApplication::translate("Agent", "-----SNMP query finished-----<br>");
    msg += QCoreApplication::translate("Agent", "<font color=#009000>Total # of Requests = ");
    msg += QCoreApplication::translate("Agent", "%1<br>Total # of Objects = %2</font>").arg(requests).arg(objects);
    out.statusHtml = stopped
        ? QCoreApplication::translate("Agent", "Stopped after %n object(s) in %1 request(s).", nullptr, objects).arg(requests)
        : QCoreApplication::translate("Agent", "Finished: %n object(s) in %1 request(s).", nullptr, objects).arg(requests);
    out.paragraphs << msg;
    out.objects = objects;
    out.requests = requests;
    return out;
}
}

SnmpQueryText FormatWalkEnd(const SnmpWalkResult &result)
{
    SnmpQueryText out;
    QString msg;
    const QString problem = result.problem.toHtmlEscaped();
    out.objects = result.objects;
    out.requests = result.requests;
    switch (result.status) {
    case SnmpOperationStatus::Complete:
    case SnmpOperationStatus::Cancelled: {
        const bool stopped = result.status == SnmpOperationStatus::Cancelled;
        if (!problem.isEmpty())
            msg += QCoreApplication::translate("Agent", "<font color=red>%1</font><br>").arg(problem);
        msg += stopped ? QCoreApplication::translate("Agent", "<font color=red>-----SNMP query stopped-----</font><br>")
                       : QCoreApplication::translate("Agent", "-----SNMP query finished-----<br>");
        msg += QCoreApplication::translate("Agent", "<font color=#009000>Total # of Requests = ");
        msg += QCoreApplication::translate("Agent", "%1<br>Total # of Objects = %2</font>")
                   .arg(result.requests).arg(result.objects);
        out.statusHtml = stopped
            ? QCoreApplication::translate("Agent", "Stopped after %n object(s) in %1 request(s).", nullptr, result.objects).arg(result.requests)
            : problem.isEmpty()
                ? QCoreApplication::translate("Agent", "Finished: %n object(s) in %1 request(s).", nullptr, result.objects).arg(result.requests)
                : QCoreApplication::translate("Agent", "Ended early after %n object(s) in %1 request(s).", nullptr, result.objects).arg(result.requests);
        if (!problem.isEmpty())
            out.statusHtml += QCoreApplication::translate("Agent", " <font color=red>%1</font>").arg(problem);
        break;
    }
    case SnmpOperationStatus::Timeout:
        msg = problem.isEmpty() ? timeoutText() : QStringLiteral("<font color=red>%1</font>").arg(problem);
        break;
    case SnmpOperationStatus::SnmpError:
        msg = !problem.isEmpty()
            ? QCoreApplication::translate("Agent", "<font color=red>%1</font>").arg(problem)
            : QCoreApplication::translate("Agent", "<font color=red>SNMP error %1: %2</font>")
                  .arg(result.snmpErrorStatus).arg(plainError(result.snmpErrorStatus));
        break;
    default:
        msg = !problem.isEmpty()
            ? QCoreApplication::translate("Agent", "<font color=red>%1</font>").arg(problem)
            : QCoreApplication::translate("Agent", "<font color=red>No response received</font>");
        break;
    }
    out.paragraphs << msg;
    return out;
}
