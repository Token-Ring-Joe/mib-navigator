# USM Credential Management v2

## Ownership and editor lifecycle

`UsmCredentialService` is the canonical non-widget credential snapshot.
`USMProfileManager` rebuilds `USMProfile` adapters from that snapshot whenever
the dialog opens. Each adapter carries the stable credential ID plus editable
security name, protocols, and existing secret values. New rows receive a new
random ID. Widget changes affect only these working copies.

Cancel discards the adapters on the next open. It does not touch the SNMP++
runtime table, `usm_users.conf`, `credential-identities.conf`, or Agent Profile
references. OK validates non-empty unique security names before persistence.

## Coordinated persistence and recovery

`UsmCredentialRuntimeRepository` remains the only SnmpB adapter that snapshots
or rebuilds the SNMP++ table and invokes `USM::save_users()`. SNMP++ continues
to own the unchanged secret-file format. `UsmCredentialRepository` persists
only stable ID/security-name mappings.

`UsmCredentialCoordinator` applies runtime/file changes first and the identity
mapping second. If either writer fails, it invokes both writers with the prior
committed records. Only after both writes succeed does the service replace its
canonical snapshot or emit change notifications.

True cross-file atomicity is impossible because SNMP++ writes
`usm_users.conf` internally and exposes no prepare/commit operation. Recovery
therefore restores the prior logical user table and mappings, but byte-for-byte
file identity is not guaranteed. If the underlying failure also prevents a
rollback write, the result is explicitly `RollbackFailed`; the editor reports
a non-secret save failure and does not adopt the working copy.

## Rename, delete, and references

Rename preserves `credentialId`. After credential persistence succeeds,
unambiguous Agent Profiles referencing the old `secname` are updated through
`AgentProfileService`; `agents.conf` retains its existing schema. Duplicate
legacy security names are never guessed.

Delete assessment counts Agent Profile references. Referenced deletion needs
explicit confirmation. Confirmed deletion never deletes or retargets Agent
Profiles; their old security name becomes a detectable missing reference.

The Agent Profile editor retains missing security names in its combo box and
shows a non-secret status: available, missing, ambiguous, empty, or incompatible
with the chosen security level. Credential IDs and secrets are not displayed.

## Notifications

The service emits created, updated, renamed, and deleted signals carrying the
stable credential ID, followed by `credentialsChanged`. The manager emits a
presentation-level accepted-change notification so Agent Profile security-name
choices refresh without consulting USM editor widgets.

## Secret handling

No secret representation changed. USM passwords remain hex encoded rather than
encrypted in `usm_users.conf`; communities remain plaintext in `agents.conf`.
The identity sidecar, validation text, notifications, model roles, logs, and
portable transfer contain no secret values. Existing Qt/SNMP++ working copies
remain in memory for the duration required by the legacy editor/runtime.

## Reusable community credentials

A future explicit migration should add an optional community credential ID to
Agent Profiles while retaining inline read/write communities as the default
legacy path. Missing IDs must not invalidate inline values. Migration should be
user initiated; duplicate profiles retain the reference or inline values, and
referenced deletion requires the same assessment/confirmation policy as USM.

Community and USM credentials should share a small identity/health facade but
retain strongly typed records and secret repositories. Their secret payloads
have different compatibility formats and should not be forced into one file.
Portable transfer should continue omitting secrets and may carry only an
unresolved reference descriptor under an explicit future format revision.
