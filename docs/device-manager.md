# Device Manager v1

Agent profiles remain the authoritative SNMP endpoint and request-settings
records and continue to be stored in `agents.conf`. The Device Manager adds an
organizational model around those records; it does not copy their settings.

## Sidecar schema

`device-tree.conf` is a QSettings INI file with schema version 2:

```text
schema/version
folders/N/id
folders/N/parent
folders/N/name
folders/N/order
folders/size
profiles/N/id
profiles/N/parent
profiles/N/profileId
profiles/N/order
profiles/size
```

Folder and profile-reference nodes have stable UUIDs. `parent` contains a
folder UUID. `profileId` contains the stable opaque identity stored with the
authoritative Agent Profile.

The file never contains addresses, ports, communities, USM secrets, retry or
timeout values, protocol flags, bulk settings, or context values.

## Reconciliation

Loading discards empty or duplicate node IDs, duplicate profile-ID references,
references to missing profiles, and placements with invalid parents. Invalid
folder parents and cycles are promoted to the root. Profiles without a valid
placement appear under the derived **Unfiled** node. Unfiled is never persisted.

Deleting a folder removes its organizational subtree and placements only. Its
Agent Profiles remain in `agents.conf` and consequently reappear under
Unfiled. Newly added or externally added profiles behave the same way.

Version 1 sidecars used `profile=<display name>`. A legacy reference migrates
only when exactly one Agent Profile has that name. Ambiguous or unresolved
references are not guessed: all matching profiles remain available under
Unfiled. A subsequent legitimate organizational save writes version 2.

Profile display names are not identity. Same-name profiles with distinct IDs
are distinct Device Manager entries and can be organized independently.
