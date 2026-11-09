# Credential Architecture v1

## Ownership and persistence audit

`AgentProfileRecord` owns the SNMPv1/v2c read and write communities and stores
them as plaintext `QString` values in `agents.conf`. It also stores SNMPv3
`secname`, security level, context name, and context engine ID. The latter are
configuration/reference values, not USM secrets. Duplicate and Discovery clone
these inline values through the profile record. Portable profile transfer
intentionally clears communities, exports only the security-name reference,
and marks credentials omitted.

SNMPv3 authentication/privacy protocols and password bytes live in SNMP++'s
USM user-name table. Startup calls `USM::load_users()` and accepted edits call
`USM::save_users()`. SnmpB chooses the path and rebuilds the runtime table, but
SNMP++ alone parses and writes `usm_users.conf`. Its fixed line format hex
encodes passwords; encoding is not encryption. Adding an ID field would change
SNMP++ persistence and is therefore not compatible.

`USMProfileManager` currently mirrors the runtime table in mutable QObject and
tree-item-backed `USMProfile` instances. Authentication/password/privacy edits
mutate these dialog-owned objects immediately. Cancel avoids rebuilding and
saving the runtime table, but the dialog objects remain the manager's working
state. Accepted edits delete and rebuild the complete SNMP++ user table.

The request path copies inline communities or `secname`/security level into
`SnmpRequestConfig`; SNMP++ target construction consumes only that resolved
value. Authentication and privacy secrets remain in the USM runtime table and
are not copied into request configuration. Discovery uses the same inline
community/security-name representation. Graph compatibility paths use the
same request configuration boundary.

## V1 boundary

`CredentialIdentity`, `CommunityCredentialRecord`, and `UsmCredentialRecord`
are widget- and session-independent. Internal opaque IDs are separate from
display/security names. Secret values use a dedicated type with no debug or
generic serialization operator. `CredentialResolver` produces effective
values before `SnmpRequestConfig`, so low-level operation loops need no
repository access.

`UsmCredentialRepository` stores only credential ID/security-name mappings in
`credential-identities.conf`. Reading never creates or rewrites the file.
`UsmCredentialService` reconciles a runtime snapshot conservatively: only one
runtime record and one stored mapping may match by name. Duplicate legacy names
remain distinct and references are reported ambiguous. Explicit rename keeps
the stable ID; delete leaves Agent Profile references missing and does not
rewrite or destroy profiles. Security-level validation reports missing auth or
privacy configuration without preventing legacy profile loading.

SNMP++ runtime snapshot/rebuild/save operations now sit behind
`UsmCredentialRuntimeRepository`, so widgets no longer implement the canonical
persistence adapter. The manager still owns dialog working records.

The sidecar is intentionally not yet wired into `USMProfileManager` writes.
Doing so safely requires carrying credential IDs through each editor working
copy and coordinating two-file commit failure behavior with SNMP++'s existing
save operation. V1 establishes and tests that boundary without risking secret
persistence or runtime behavior. The legacy manager therefore remains the
canonical editor/runtime adapter for this milestone.

## Security review

- Communities are plaintext in `agents.conf` and remain ordinary `QString`
  values in memory.
- USM password bytes are hex encoded, not encrypted, in `usm_users.conf`.
- SNMP++ clears password buffers when user entries are destroyed, but legacy
  Qt editor copies remain in `QString` storage and are not explicitly wiped.
- Portable transfer exports neither communities nor USM passwords.
- Profile duplication copies inline communities and the USM name reference;
  it does not create or duplicate a USM user.
- The identity sidecar contains no secrets and IDs are random UUIDs, never
  derived from names or secret material.
- New credential records have no debug/model/export formatter, preventing
  accidental generic disclosure through the new boundary.

## Next migration

USM Credential Management v2 converts `USMProfileManager` to service-backed
working copies carrying stable IDs, adds coordinated save/error handling, and
surfaces non-secret missing-reference status. Agent Profiles retain legacy
inline communities and `secname`. Later schemas may add optional
credential IDs: an explicit ID resolves a reusable credential, while absent IDs
continue using legacy inline/name behavior. Migration must be user initiated,
never an automatic startup rewrite.

Community and USM records should remain strongly typed even if their non-secret
identity mappings share a repository. Deletion must be blocked or explicitly
confirmed when referenced; rename must preserve IDs. Imports may preserve an
unresolved reference ID/name but must never manufacture or export secrets.
Future OS vault/keychain integration should store secret payloads behind the
same service boundary, after a separate cross-platform security design.
