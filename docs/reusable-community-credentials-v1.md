# Reusable community credentials v1

Reusable SNMPv1/v2c communities are an optional layer over the existing Agent
Profile fields. Existing profiles remain in inline mode unless a user binds a
profile explicitly. No automatic migration occurs, and `agents.conf` retains
its existing read/write community fields for compatibility with older SnmpB
versions.

## Persistence

`community-credentials.conf` is a QSettings INI file with schema version 1.
It contains a `credentials` array whose records have:

- `credentialId`: opaque UUID and authoritative identity
- `displayName`: user-facing, non-unique label
- `readCommunity`: secret-bearing byte value
- `writeCommunity`: secret-bearing byte value

`credential-bindings.conf` is a separate QSettings INI file with schema
version 1. Its `bindings` array maps stable `profileId` values to stable
`credentialId` values. It contains no community values. Missing files mean an
empty repository, and loading either repository does not create or rewrite it.

Community strings in `community-credentials.conf` have protection equivalent
to the existing plaintext communities in `agents.conf`. They are not encrypted
or otherwise protected by this feature. File-system permissions remain the
only storage protection.

## Resolution and lifecycle

For SNMPv1/v2c, a valid binding resolves to the reusable credential. Without a
binding, the existing inline profile values are used unchanged. A binding to a
missing credential remains recorded for diagnosis and reports a missing
reusable credential; requests fall back explicitly to the retained inline
values for backward compatibility.

Renaming a profile or credential does not change a binding. Duplicating a
profile shares the same reusable credential rather than duplicating secrets.
Deleting a profile removes its binding. Credential deletion is reference-aware
and requires confirmation when referenced; confirmed deletion does not remove
profiles or retarget bindings, leaving the missing reference visible.

Discovery-created profiles inherit the template profile's binding and retain
the existing inline clone values. Updating a reusable credential affects the
next request made by every bound profile. An already captured
`SnmpRequestContext` retains the effective values with which it began.

## UI and non-secret health

The Agent Profile editor offers explicit Inline and Reusable sources. Reusable
values are not displayed there; the inline fields remain stored for legacy and
fallback use. Binding edits use a dialog working copy, so Cancel writes neither
profiles nor bindings. The Community Credential Manager masks its value fields
and applies its working copy only on OK; masking is a display treatment, not
encryption.

Device Manager exposes non-secret health through a model role, tooltip, and
search text. Health reports inline, available, missing, or unusable status but
never includes community strings or USM passwords.

## Portable transfer

Portable profile transfer remains secret-free. It exports neither reusable
credential records nor binding identifiers, and import creates no reusable
credentials or bindings. Imported profiles therefore use locally configured
inline values or a reusable credential selected locally after import.
