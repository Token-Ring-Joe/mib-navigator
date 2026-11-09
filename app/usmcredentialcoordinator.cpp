#include "usmcredentialcoordinator.h"

UsmCommitResult UsmCredentialCoordinator::apply(
    const QList<UsmCredentialRecord> &before,
    const QList<UsmCredentialRecord> &after,
    const Writer &runtimeWriter, const Writer &identityWriter)
{
    if (!runtimeWriter(after))
    {
        const bool restored = runtimeWriter(before);
        return {restored ? UsmCommitStatus::RuntimePersistenceFailed
                         : UsmCommitStatus::RollbackFailed,
                true};
    }
    if (!identityWriter(after))
    {
        const bool runtimeRestored = runtimeWriter(before);
        const bool identityRestored = identityWriter(before);
        return {runtimeRestored && identityRestored
                    ? UsmCommitStatus::IdentityPersistenceFailed
                    : UsmCommitStatus::RollbackFailed,
                true};
    }
    return {};
}
