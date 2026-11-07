# MIB Navigator settings and state ownership

Reviewed against the current implementation on 2026-11-07. Preferences retain
user-scope INI `QSettings`, organization domain `snmpb.sourceforge.net` and
application name `SnmpB`. Do not rename this identity as a branding change.
Platform locations come from Qt, not hard-coded home/AppData paths.

## Current settings

| Key | Read default | Owner/use |
|---|---|---|
| `network/enableipv4`, `network/enableipv6` | `true` | Trap transport families; listener changes require restart |
| `network/trapport4`, `network/trapport6` | `162` | IPv4/IPv6 trap listener ports |
| `ui/expandtrapbinding` | `true` | Expand selected trap bindings |
| `ui/traphistorylimit` | `1000` | In-memory trap bound, clamped to 1–100000; applies on save |
| `misc/showagentname` | `false` | Optional hostname lookup alongside addresses in Query output |
| `request/port` | `161` | Global request port unless Target includes one |
| `request/timeout` | `3` | Global request timeout in seconds |
| `request/retries` | `1` | Global retry count |
| `request/nonrepeaters` | `0` | Global GETBULK non-repeaters |
| `request/maxrepetitions` | `10` | Global GETBULK max-repetitions |
| `connection/target-history` | empty | JSON array of up to 30 `{target, credential}` entries; no MIB Profile or secrets |
| `ui/theme` | `system` | Appearance controller: `system`, `light`, `dark` |
| `mib-library/root` | `DocumentsLocation/MIB Navigator/MIBs` | User-visible working collection |
| `mib-library/current-profile` | built-in Standards/MIB-II ID | Active Query/interpretation Profile; distinct from Profiles' Edit profile selector |
| `mib-library/migration-v1-complete` | `false` | Successful safe legacy collection initialization/copy marker |
| `mib-library/inventory-details-splitter` | initial 11:9 split | Library inventory/details splitter bytes |
| `mib-library/dependencies-status-width` | quarter of viewport, minimum 120 | Dependency status column width |
| `mainwindow/size`, `mainwindow/pos` | absent | Main-window geometry; restoration is repaired against available screens |
| `mainwindow/state` | absent | Qt toolbar/dock state, followed by workbench presentation repair |
| `workbench/inspector-shown` | `true` | Query Inspector visibility, retained while other workspaces hide it |
| `workbench/tree-splitter` | initial pane sizes | Query horizontal splitter bytes; invalid/stale orientation cannot replace the layout |
| `workbench/panel-visible` | `false` | Shared Operations panel expansion |
| `workbench/panel-height` | `300` | Expanded panel height, bounded on restoration |
| `workbench/panel-tab` | `output` | Shared Output/Log selection, stored by stable ID |
| `workbench/mib-panel-tab` | shared/legacy selection | Independent MIBs panel selection; Problems waits for the initial report |

Preferences stages edits until OK. A currently running operation keeps the
request settings/context it captured. A Watch row retains its Target and
Credential reference; credential values are resolved again for subsequent polls.
The planned SNMPv3 context-name preference is not implemented: current
connection-bar requests use the empty default context.

## Retained historical settings

Old selected Agent Profile/protocol keys, Discovery/Graphs settings and saved
device files are not current connection authority. Existing `agents.conf` is
not read/migrated by the connection bar and is left untouched.

`mibpaths` and `mibpreloads` arrays remain readable as historical/migration data;
normal Preferences saves preserve them. They do not supply hidden provider
precedence or global parser search paths. The visible Library collection supplies
runtime paths; the active Profile plan authorizes loaded providers. `ui/horizontalsplit`
and `misc/automaticloading` are retained compatibility values, not current layout
or Profile authority. Old `workbench/activity`, `workbench/content-splitter`,
`workbench/bottom-visible` and `workbench/bottom-tab-name` values are retained.
Restoration itself does not rewrite old panel selections; normal save records
stable IDs, including safe migration of the former shared Problems choice.

## User files and private bookkeeping

The configured collection reserves `Standards` and `Unassigned` as global trees.
Each `Vendor/Product` folder creates an Automatic Profile. Files are never edited
or normalized by a Library refresh; provider identity hashes raw bytes.
Bundled installation MIB/PIB files seed the collection and remain package resources.

Custom profile definitions live in `profiles-v1.json` under
`MibCollection::legacyManagedRoot()` (AppLocalDataLocation/mibs); followed folders
are additive state in `profile-folders-v1.json`. Automatic Profiles are derived
from physical folders rather than persisted as another editable membership list.
Indexes, caches and catalogs belong in application data storage, outside the
collection. See `mib-architecture.md` for the detailed ownership model.

The settings directory owns `smi.conf`, `boot_counter.conf`, `log.conf`,
`usm_users.conf`, `credential-identities.conf` and `community-credentials.conf`,
with their existing independent schemas. `smi.conf` is generated from collection
paths. Retired `agents.conf`, `device-tree.conf`, `profile-metadata.conf`,
`credential-bindings.conf` and `graphs.conf` may still exist and are not deleted.
No mutable configuration belongs in an install tree. Query Macros has an agreed
design; no macro settings/file schema has been implemented.
