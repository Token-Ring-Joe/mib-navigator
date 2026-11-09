#include "udpportowner.h"

#include <QCoreApplication>
#include <QFileInfo>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <vector>
#endif

UdpPortOwner UdpPortOwnerLookup::lookup(quint16 port, bool ipv6)
{
    UdpPortOwner result;
#ifdef Q_OS_WIN
    ULONG size = 0;
    const ULONG family = ipv6 ? AF_INET6 : AF_INET;
    GetExtendedUdpTable(nullptr, &size, FALSE, family, UDP_TABLE_OWNER_PID, 0);
    if (!size) return result;
    std::vector<unsigned char> storage(size);
    if (GetExtendedUdpTable(storage.data(), &size, FALSE, family,
                            UDP_TABLE_OWNER_PID, 0) != NO_ERROR)
        return result;
    DWORD pid = 0;
    if (ipv6) {
        const auto *table = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID *>(storage.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
            if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port) {
                pid = table->table[i].dwOwningPid; break;
            }
    } else {
        const auto *table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID *>(storage.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
            if (ntohs(static_cast<u_short>(table->table[i].dwLocalPort)) == port) {
                pid = table->table[i].dwOwningPid; break;
            }
    }
    if (!pid) return result;
    result.found = true; result.processId = pid;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        std::vector<wchar_t> path(32768);
        DWORD length = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
            result.executablePath = QString::fromWCharArray(path.data(), length);
            result.processName = QFileInfo(result.executablePath).fileName();
        }
        CloseHandle(process);
    }
#else
    Q_UNUSED(port); Q_UNUSED(ipv6);
#endif
    return result;
}

QString UdpPortOwnerLookup::conflictDescription(quint16 port,
                                                const UdpPortOwner &owner)
{
    if (!owner.found)
        return QCoreApplication::translate("UdpPortOwnerLookup",
            "UDP port %1 is already in use. Owning process could not be determined.")
            .arg(port);
    QString text = QCoreApplication::translate("UdpPortOwnerLookup",
        "UDP port %1 is already in use.\nProcess: %2\nPID: %3")
        .arg(port).arg(owner.processName.isEmpty() ?
            QCoreApplication::translate("UdpPortOwnerLookup", "Unknown") :
            owner.processName).arg(owner.processId);
    if (!owner.executablePath.isEmpty())
        text += QCoreApplication::translate("UdpPortOwnerLookup", "\nPath: %1")
                    .arg(owner.executablePath);
    if (owner.processName.compare(QStringLiteral("mib-navigator.exe"),
                                  Qt::CaseInsensitive) == 0)
        text.prepend(QCoreApplication::translate("UdpPortOwnerLookup",
            "Another instance of MIB Navigator is already listening for SNMP traps.\n"));
    return text;
}
