# Agent Profile Management v2

## Ownership audit

Before this milestone, `AgentProfileManager` combined five responsibilities:
repository persistence, canonical mutable `AgentProfile` objects, a
`QTreeWidgetItem` hierarchy used as storage/navigation, dialog presentation,
and lookup APIs used by Query and Discovery. Widget signals changed the
canonical records immediately; Cancel restored a snapshot after the fact.

`AgentProfileRepository` was already a value-based serializer, but consumers
still reached through `AgentProfileManager` to dialog-owned objects. Broad
`AgentProfileListChanged` notifications caused each consumer to repopulate
from the manager. The editor also reads the USM manager's display-name list to
populate its SNMPv3 security-name combo; credentials remain owned by the USM
subsystem.

## Current boundary

`AgentProfileService` is the application owner of `AgentProfileRecord` values.
It loads through `AgentProfileRepository`, implements ID-based create, update,
delete, and duplicate semantics, persists successful mutations, and emits
identity-bearing Qt signals. Duplicate display names remain valid; unique-name
lookup is explicitly distinguishable from the legacy first-name adapter.

The existing manager dialog now receives working copies. Widget changes and
tree items affect only those copies. OK reconciles them through the service;
Cancel discards them and repopulates from the service. A new-profile draft is
not created in the service until OK, and deletion is applied only when the
accepted working set omits the explicitly removed record. The legacy
`AgentProfile` QObject remains temporarily as a dialog adapter, not canonical
application storage.

Query and Discovery refresh their combos from service-owned records via the
manager's compatibility list API and retain independent selections by stable
ID. Their request setup no longer retrieves a dialog-owned `AgentProfile`.
The disabled Qwt graph path still uses the legacy name adapter and should be
migrated if graphing is ported to Qt 6.

## Device pane

The Devices pane uses a recursive `QSortFilterProxyModel` over
`DeviceTreeModel`. Search matches profile name and address/hostname while
retaining matching ancestors. New, edit, duplicate, and profile-delete actions
are distinct from folder deletion. Folder deletion only removes organization;
profiles become Unfiled. Profile deletion calls the profile service and the
tree reconciliation removes its placement. Creation requested on a folder is
placed there after the accepted service mutation.

## Compatibility and next metadata boundary

No persistence schema changed. `agents.conf` remains serialized exclusively by
`AgentProfileRepository`, including optional stable IDs and legacy migration.
`device-tree.conf` remains stable-ID organization metadata. Neither file is
rewritten merely by opening the editor.

Future notes, tags, and optional MIB associations describe a profile rather
than its tree placement. They should use a small profile-metadata sidecar keyed
by `profileId`, leaving `agents.conf` backward compatible and
`device-tree.conf` focused on folders and placement. Import/export can compose
the repository record, metadata, and optional placement. A Discovery
destination folder belongs in Discovery/UI preferences as a folder ID. SQLite
is not justified for these bounded configuration records.
