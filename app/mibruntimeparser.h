#ifndef MIBRUNTIMEPARSER_H
#define MIBRUNTIMEPARSER_H

#include "mibeffectiveplan.h"

#include <functional>

struct MibRuntimeParserResetResult {
    bool success = false;
    QString error;
    QStringList appliedPaths;
    int restoredFlags = 0;
};

enum class MibExplicitRootLoadStatus {
    LoadedByIdentity,
    LoadedByAlias,
    AlreadyLoaded,
    MissingAliasFile,
    AliasContentChanged,
    RequestedIdentityNotDeclared,
    NativeLookupFailed,
    ParserError,
    UnauthorizedAlias
};

struct MibExplicitRootLoadResult {
    QString identity;
    MibExplicitRootLoadStatus status = MibExplicitRootLoadStatus::NativeLookupFailed;
    QString physicalPath;
    QString diagnostic;
    bool success = false;
};

struct MibExplicitRootLoadBatch {
    QList<MibExplicitRootLoadResult> roots;
    QStringList failedIdentities() const;
};

class MibRuntimeParser final
{
public:
    static MibRuntimeParserResetResult reset(
        const MibRuntimePathConfiguration &paths,
        const std::function<void()> &restoreApplicationConfiguration = {});
    static MibExplicitRootLoadBatch loadExplicitRoots(
        const MibProfileRuntimeConfiguration &configuration,
        const MibRuntimePathConfiguration &paths);
};

#endif
