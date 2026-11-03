# Unified credential management

The current product direction is `redesign-plan.md`, step 18a-7. This replaces the
older tabbed USM editor and username-unique runtime model.

## Ownership and identity

`UsmCredentialService` and `CommunityCredentialService` own saved records.
`CredentialsDialog` owns disposable working copies in one list, keyed by credential
kind and stable ID. The right-hand form shows common display name/version plus
community fields or USM username/authentication/privacy fields. New chooses the
version. Duplicate creates a new ID while retaining the wire username and secrets.
Delete affects only the selected working entry until Save; Cancel changes neither
service nor files. The connection bar labels v3 entries by display name and resolves
requests by saved ID, never by username or the currently selected list row.

Legacy community entries have protocol `-1` and continue offering their existing
v1 and v2c choices with unchanged history keys. New entries use `0` (v1) or `1`
(v2c). Changing type is done by creating another entry; existing references remain
attached to their original credential ID.

## Migration and persistence

Legacy `usm_users.conf` is read through the patched SNMP++ adapter, and the legacy
identity sidecar supplies any existing IDs. Unmapped imported usernames receive
deterministic namespace UUIDs so read-only opening and restart preserve references.
The first confirmed save writes full v3 records to the identity path plus
`.v2.json`; neither legacy file is rewritten. After that the new file is the only
saved v3 record authority, including empty stores after deleting all v3 entries.
Invalid or unknown-schema stores produce a visible error and block saving; there
is no silent fallback to old credentials or empty data.

Community files retain their INI schema, adding only an optional protocol field.
Both repositories write with `QSaveFile` and owner read/write permissions. Save
writes v3 first, then communities; a failed second write restores the previous v3
snapshot. Neither service is published until persistence succeeds. Failure leaves
the editor open with a non-secret explanation. A rollback write failure is reported
separately and the in-memory services still retain the old state. No-op saves avoid
rewrites, except materializing the first explicit v3 migration.

Secrets remain reversibly encoded local data, as in the legacy application: v3
secrets use base64 in the new protected store; community values keep Qt's INI byte
encoding. This is not encryption or an operating-system keychain. The legacy
identity sidecar, labels, notifications and request logs contain no secrets.

## Request and security context lifetime

`ConnectionModel` copies the chosen complete v3 record into `SnmpRequestConfig`.
`SnmpPlusTransport` binds its session to a security context keyed by the saved ID,
wire username, protocols and secrets. A bounded cache preserves discovery state
and privacy salts across polls. A secret/protocol edit creates another context;
operations already running keep their captured snapshot. Display-only renames
reuse the existing context. Watch continues resolving its captured credential ID
at each poll; edits apply on the next poll, and deletion reports a missing
credential rather than retargeting.

The vendored SNMP++ change is opt-in: sessions and messages can use an independent
v3MP; legacy callers still default to `v3MP::I`. Send, receive, retry, engine
cache and timeout cleanup all use the session's context. Private contexts never
replace or clear the application's notification context. libsmi is untouched.

## Incoming SNMPv3 notifications

The legacy default notification context keeps the first saved variant for each
wire username. A successful v3 request additionally registers that credential's
localized keys for the remote engine ID, so known devices with the same username
can use different protocols/secrets. An unsolicited notification using another
variant before any successful query in the current session uses the default and
may fail authentication. Editing or deleting any variant clears learned notification
keys, including variants outside the default entry; display-only renames preserve
that state. Re-query devices to establish their variant mappings again after a
security change. This is an explicit remaining limitation for new
same-username notification variants; single-variant notification behavior remains.

## Validation findings (2026-11-02)

The focused credential/dialog/connection/wire tests pass with leak detection
and AddressSanitizer enabled. The full application's headless startup/restart
workflow also passes AddressSanitizer's invalid-access checks with leak detection
disabled. These results must not be described as a leak-free application:
strict full-app LeakSanitizer reports 101,206 bytes in 1,919 allocations, mostly
libsmi diagnostic buffers (`libsmi/lib/error.c:printError`), plus older Agent UI
and MIB Editor ownership paths. Those source paths were not changed by this
credential work; the separate report is `/tmp/unified-credentials-asan-app-test.log`.
Parser/legacy UI leak cleanup remains a separate workstream.

A pre-existing Walk responsiveness assertion failed once during concurrent
compilation and then passed ten immediate repeats. It bounded wall-clock timer
gaps, including VM scheduling pauses. The test now observes queued heartbeats at
multiple intermediate delivery positions instead, proving event-loop interleaving
without a machine-speed assumption; ten consecutive runs of the revised test pass.
