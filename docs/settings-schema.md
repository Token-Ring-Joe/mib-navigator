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
| `request/contextname` | empty | Exact UTF-8 SNMPv3 context, captured by new operations; ignored by v1/v2c |
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
New Watch definitions also capture the resolved target port and context name.
Changing Preferences cannot redirect them to another port/context. Each new poll
resolves the saved credential reference again; active Profile interpretation stays
under the existing published Environment owner.

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
`usm_users.conf`, `credential-identities.conf`, `credential-identities.conf.v2.json`
and `community-credentials.conf`, with their existing independent schemas. The
`.v2.json` file is the canonical SNMPv3 record store; historical `usm_users.conf`
is migration input, not modern credential-editor authority. `smi.conf` is generated from collection
paths. Retired `agents.conf`, `device-tree.conf`, `profile-metadata.conf`,
`credential-bindings.conf` and `graphs.conf` may still exist and are not deleted.
No mutable configuration belongs in an install tree. Query Macros definitions use
their own file, described below.


## Saved Watch definitions

`watches-v1.json` lives under the existing Qt `AppLocalDataLocation`. Isolated
headless runs put it below `snmpb.isolatedStateRoot`; tests never use real stores.
The non-widget `WatchService` owns both active definitions and persistence.
Schema version 1 contains `schemaVersion: 1` and an ordered `watches` array.
Each item has `target` (resolved port included for UI-created rows), `credential`
(stable reference only), numeric instance `oid`, captured `context`, and integer
`intervalSeconds` (1–3600). No secrets, values, counter baselines, polling status,
Profile copy or parser pointers are persisted. Runtime row IDs are session-local.

Definitions are saved atomically using `QSaveFile`, with owner read/write
permissions, after add/remove/interval edits. A lock coordinates app writers;
byte comparison detects external edits. No-op saves avoid rewrites. A failed save
leaves rows available with a visible error; a later edit retries. Invalid/newer
files remain untouched and block saving until fixed and reloaded by restart.
Loading validates the whole file before adding rows. Bounds: 10,000 definitions
and 16 MiB. Remove all persists an empty collection. Every restored row is paused;
Resume/Poll now is explicit, and rates start from a new sample baseline.

## Query Macros definitions (schema v1)

`query-macros-v1.json` resides in `QStandardPaths::AppLocalDataLocation`, alongside
`watches-v1.json` (isolated acceptance uses its private state root). Schema version
1 stores ordered macros and entries with stable IDs, names, numeric object/complete
instance/row OIDs, optional labels, ordered columns, instance policies and fixed
numeric suffixes. Scalar object OIDs exclude `.0`; exact instances remain complete.
No Target, Credential, secret, MIB Profile, MIB schema, provider selection, schedule
or result is persisted. It does not migrate the old Varbinds dialog.

Save is explicit and atomic with QSaveFile, a transient `.lock` and SHA-256
comparison against the loaded file. Failed saves keep working edits; external
changes are not overwritten. Malformed, oversized, unknown-field and newer-schema
files remain untouched and block editing until explicitly repaired/reloaded.
Opening the editor, selecting objects and restarting send no requests. State and
lock files are excluded from deployment. See [milestone notes](query-macros-milestone-1.md).

Macro runs are not persisted. Milestone 2 captures the definition, resolved
request settings/credential values and published immutable MIB Environment when
Run is clicked. Later edits and connection/Profile changes affect subsequent
runs. Scalar objects read `.0`; exact instances use their saved numeric OID.
Results and Log contain copied presentation/operation values, not secrets.
Table retrieval remains deferred. Definition schema v1 is unchanged; this
milestone does not migrate or rewrite existing stores merely to enable Run.
