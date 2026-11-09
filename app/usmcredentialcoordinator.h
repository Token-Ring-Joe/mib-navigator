#ifndef USMCREDENTIALCOORDINATOR_H
#define USMCREDENTIALCOORDINATOR_H

#include "credentialrecords.h"

#include <functional>

enum class UsmCommitStatus
{
    Success,
    RuntimePersistenceFailed,
    IdentityPersistenceFailed,
    RollbackFailed
};

struct UsmCommitResult
{
    UsmCommitStatus status = UsmCommitStatus::Success;
    bool rollbackAttempted = false;
};

class UsmCredentialCoordinator
{
public:
    using Writer = std::function<bool(const QList<UsmCredentialRecord> &)>;

    static UsmCommitResult apply(const QList<UsmCredentialRecord> &before,
                                 const QList<UsmCredentialRecord> &after,
                                 const Writer &runtimeWriter,
                                 const Writer &identityWriter);
};

#endif
