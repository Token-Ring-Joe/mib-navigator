# Device Manager v3: preferred MIBs and Discovery placement

## MIB association audit and semantics

MIB search paths are stored in the `mibpaths` QSettings array and preload
identifiers in `mibpreloads`. Automatic walk loading uses
`misc/automaticloading`. `MibModule::Refresh()` rereads those settings, exits
and reinitializes libsmi, then reconstructs the global MIB views. That lifecycle
is intentionally not used for profile selection.

libsmi exposes the canonical module name as `SmiModule::name`. The module scan
now retains those names as value strings for editor choices; no paths, MIB
contents, `SmiModule *` values, or parser state enter profile metadata.
Unavailable names remain valid profile intent.

`ProfileMetadataRecord::preferredMibs` is an ordered, duplicate-free list of
trimmed canonical module names. Empty means no profile-specific preference.
`profile-metadata.conf` schema version 2 adds `preferredMibs` to each profile
record. Version 1 is read as an empty list, so no migration write is required.
Rename remains irrelevant because metadata is keyed by stable `profileId`.

The Agent Profile Information page provides a checkable list of known module
names. Names that were saved but are currently unavailable remain visible and
checked. The list participates in the same working-copy transaction as notes
and tags: Cancel writes nothing, OK persists it, duplicate copies it to the new
profile ID, and delete removes it with the metadata record.

Loading is explicit through the Devices context command **Load Preferred
MIBs**. It loads only missing requested modules, keeps already loaded and
unrelated modules, does not change search paths or global preload preferences,
and never unloads a module. Missing modules are logged and do not invalidate
the profile. Preferred names are included in Device search and tooltips.

## Discovery destination folders

Discovery previously cloned the selected Agent Profile through the manager and
allowed Device Manager reconciliation to show each new profile under Unfiled.
The new flow is:

```text
Discovery selected template
    -> AgentProfileService::createFromTemplate()
    -> new stable profileId
    -> DeviceTreePlacementService::placeProfile(profileId, folderId)
```

The Discovery tab adds a destination combo containing Unfiled plus nested
folder paths such as `Datacenter / Core`. Combo item data contains the stable
folder ID; display text is never identity. The local QSettings key is
`discovery/destinationfolderid`.

No key or a missing/deleted folder resolves to Unfiled. Folder rename retains
selection because its ID is unchanged. Folder organization changes refresh the
combo.

One destination ID is captured before a selected batch is created, so all
successful profiles in that operation use the same folder. Profiles with the
same display name receive distinct profile IDs. Creation failure produces no
placement. Missing-folder or persistence failure leaves the successfully
created Agent Profile intact, allowing normal reconciliation to show it under
Unfiled. Successful placement emits a refresh notification; Discovery and
Query profile combos remain independent.

## Portable transfer

Transfer schema version 2 includes `preferredMibs` in metadata. Version 1 is
still accepted and yields empty associations. Import profile-ID remapping also
remaps the containing metadata record, preserving its preferred module names.
Only names are exported, never MIB paths or contents. Credential omission is
unchanged. Discovery destination is local UI state and is not exported.

## Read-only credential/USM audit

SNMPv1/v2c communities are fields of every `AgentProfileRecord` and are stored
directly in `agents.conf`. They are duplicated with a profile and have no
reusable identity.

SNMPv3 Agent Profiles store `secname` as a name reference. The referenced USM
user is owned by SNMP++'s in-memory USM table and persisted in `usm_users.conf`
through `USM::save_users()`. `USMProfileManager` mirrors users as mutable
QObject/tree-item records containing security name, authentication
protocol/password, and privacy protocol/password. On acceptance it deletes the
complete live USM table, rebuilds it, and saves it.

References have no stable credential ID. Renaming a USM security name does not
update Agent Profiles, and deleting a USM user can leave `secname` references
dangling. Deleting an Agent Profile does not affect a USM user. Passwords are
QString/widget values during editing; the legacy SNMP++ file format owns their
on-disk protection characteristics. Portable export excludes them.

A future reusable credential layer should introduce a stable credential ID and
plain record/repository/service boundaries. Community and USM credentials can
share lifecycle APIs while retaining protocol-specific payloads. Agent Profiles
should reference the credential ID, with legacy adapters for communities and
`secname`. Migration must avoid rewriting or weakening existing secret storage,
resolve duplicate names conservatively, and define rename/delete referential
integrity before UI migration.
