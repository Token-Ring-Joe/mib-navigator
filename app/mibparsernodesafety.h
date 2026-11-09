#ifndef MIBPARSERNODESAFETY_H
#define MIBPARSERNODESAFETY_H

#include "smi.h"

#include <QList>
#include <cstdint>

inline bool MibParserNodeHasReadableOid(const SmiNode *node)
{
    if (!node || (node->oidlen != 0 && node->oid == nullptr)) return false;
    // libsmi represents SPPI INSTALL-ERRORS and SUBJECT-CATEGORIES pseudo-
    // objects by storing their numeric identifier (1..65536) in this pointer.
    return node->oidlen == 0 || reinterpret_cast<std::uintptr_t>(node->oid) > 65536;
}

inline QList<quint32> MibParserNodeOidParts(const SmiNode *node)
{
    QList<quint32> result;
    if (!MibParserNodeHasReadableOid(node)) return result;
    result.reserve(node->oidlen);
    for (unsigned int i = 0; i < node->oidlen; ++i) result.append(node->oid[i]);
    return result;
}

#endif
