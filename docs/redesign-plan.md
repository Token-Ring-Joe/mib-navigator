# MIB Navigator Redesign Plan

Status: **Accepted direction (2026-10-30)**. This document is the authority for
product direction and build order. Where older design documents in `docs/`
disagree with it, this document wins. Standing engineering rules remain in
`AGENTS.md`.

## Current checkpoint — 2026-11-08

Phase 4 is active. The main visual refresh, Menus/Settings/Help, Library/Profiles
cleanup and dialog consistency are implemented. The 2026-11-07 finishing pass
addresses empty/busy guidance, keyboard/focus access and safe remembered layouts.
Its complete headless Linux validation passes; the exact results and fresh
validated artifact are recorded in
[finish-review-validation.md](finish-review-validation.md). Earlier validation
reports are historical checkpoints, and their temporary install paths may have
been lost at reboot.

### MIBs page rework (2026-11-09, owner feedback rounds 1-3)

The Library is now viewed from a profile's point of view, because conflicts only
matter to a profile that cannot choose between copies.

- **View chooser** on the search row: Active profile, Entire collection, or any
  profile by name (no "Profile:" prefix). Status, "In Profiles", Source file and
  Filename are judged for that view. Columns are manually resizable and remembered.
- **Library folder location is a Preference** (Preferences > MIB Library), not a
  Library-page control. Changing it re-bases the Library, Profiles, search and Tree and
  asks for confirmation. The Library page keeps only Open MIB Folder.
- **One search box** on the Query page searches MIBs by name or OID; a numeric OID
  the tree knows is selected directly. The separate Go to OID box and its shortcut
  are gone.
- **Providers tab** lists the files that provide the selected module: those the viewed
  profile can reach (Used / Candidate / Older copy), with copies outside the profile
  hidden unless "Show outside copies" is ticked. File Information describes
  the file the view uses (folder, real filename), not the global record.
- **Compare Selected Copies** opens a read-only side-by-side diff. A full merge in the
  Editor remains undecided.
- **Conflict rule (owner decision 2026-11-09):** among the different copies of a module
  that a profile can reach, the copy with the strictly newest revision is used and the
  module is reported as a **Resolved conflict** (information only). Ties and copies
  without a readable revision date stay an unresolved **Conflict**. An explicit pin on a
  Custom profile still wins. **Use This Copy** pins a copy (Automatic profiles first offer
  an editable Custom copy) and leaves out the other listed copies.
- **Member state wording** on Profiles: "Changed since added" now means only that the
  file's own content changed; unused unchanged files read "Older copy" or "Conflict".
- **Responsiveness:** plan lookups are memoized and cached and Refresh scans on a worker
  thread (real Extreme collection: refresh 58 s to about 2 s, profile switch 20 s to
  about 1.4 s). The Library rescans when the window is reactivated and files changed.

[Query Macros v1](query-macros-v1-design.md) now has a concrete scope, architecture,
four-milestone sequence and effort estimate agreed by the owner on 2026-11-07.
Milestone 1 is implemented and validated. The owner authorized milestone 2
on 2026-11-08: captured scalar/exact runs, grouped Results, Stop and request
logging. See [the milestone 2 checkpoint](query-macros-milestone-2.md). Table
retrieval and legacy-dialog retirement remain milestones 3 and 4. Watch-all/table
expansion remains a separate deferred discussion.
The owner approved the accumulated work and authorized its commit on 2026-11-07.
Cross-platform release validation and any remaining native/lab acceptance are
still outstanding; approval does not claim those tests were performed. See the
[current outstanding-task inventory](outstanding-tasks.md).

The autonomous five-item follow-up (2026-11-07) implements the global context
preference, saved Watch definitions, selected-row intervals, Problems tools and
consistent library diagnostic logging. See [its validation record](autonomous-five-validation.md).
That checkpoint preceded the current Query Macros milestone. The separate
Watch-column/table design remains deferred.

The next five-item maintenance/release batch (2026-11-07) addresses Qt/unchecked
return warnings, measured diagnostic/UI leaks, broader shared value checking,
Linux archive verification and stale documentation. Its evidence/status is in
[maintenance-five-validation.md](maintenance-five-validation.md). Query Macros
milestone 1 was authorized after the resulting status review. See
[the current checkpoint and working notes](query-macros-milestone-1.md). No push, publication,
real-device test or desktop launch is included.

## Product intent

MIB Navigator is a lab and MIB-engineering tool for network engineers, not a
network management system. A session is three independent choices:

| Control | Question it answers | Lifetime |
|---|---|---|
| Target (IP or hostname) | Who am I talking to? | Throwaway, with recent history |
| Credential | How do I authenticate? | Saved and reused |
| MIB Profile | How do I interpret what comes back? | Saved; derived from the MIB collection |

## Decisions

### Retired subsystems

These are being removed. Do not extend, repair, or reintroduce them.

- **Address book / Device Manager / Agent Profiles as saved devices**, including
  device folders, tags, notes, per-device preferred MIBs, and profile
  import/export. Existing `agents.conf` data is **not migrated** and is left on
  disk untouched. The user re-enters credentials manually.
- **Discovery**, including its raw broadcast/range probe transport.
- **Graphs**, including the vendored Qwt library. Replaced by Watch.
- **Saved-walk import / offline browsing** (competitive feature #5). Out of
  scope.

Obsolete design documents (historical record only): `device-manager.md`,
`device-manager-v3.md`, `agent-profile-management-v2.md`,
`agent-profile-identity.md`, `profile-metadata-and-transfer.md`,
`discovery-v2.md`, `graphing-v2.md`.

### Connection bar

- **Target**: editable combo with a recent history of roughly 20-50 entries.
  Accepts IP addresses and hostnames. Each history entry remembers the last
  Credential used with it. It does **not** remember the MIB Profile, because a
  lab IP may be a different vendor's device next time. Entries can be deleted.
- **Credential**: dropdown of saved credentials (SNMP version plus community, or
  SNMPv3 credential display name). Managed in one Credentials list with New, Duplicate
  and Delete above the list. New chooses SNMPv1, SNMPv2c or SNMPv3, and the right
  side shows the matching fields. Display name and USM wire username are independent;
  several v3 entries may share a username with different secrets or protocols. Continue the existing
  reusable credential architecture (`credential-architecture-v1.md`,
  `reusable-community-credentials-v1.md`, `usm-credential-management-v2.md`).
- **MIB Profile**: dropdown of Automatic and Custom profiles, plus the built-in
  `Standards / MIB-II` profile, which is the default. There is **no built-in
  `All MIBs` profile** (decided 2026-10-30): loading every MIB is almost never
  wanted, and a user who does want it builds an ordinary Custom profile. An
  empty `All MIBs` Custom profile created by an earlier migration is left
  untouched as an ordinary Custom profile.
- **Detect** button next to the MIB Profile dropdown (see Phase 3).
- Port, timeout, retries, GETBULK max-repetitions, and transport are **global
  settings** reached from the menu bar. They are not shown in the main window.
  SNMPv3 context name is a global Preferences setting (implemented 2026-11-07).
  Empty keeps the default context. New operations capture its exact UTF-8 value;
  existing operations and Watch rows keep their captured context. Per-query
  context remains a later possibility.

### Traps

- Decode with the **currently selected MIB Profile**. For multi-vendor decoding
  the user selects a Custom profile that combines the vendors involved.
- Right-click **Copy source IP** copies the sender address to the clipboard.
  For SNMPv1 traps, prefer the agent-address field when it differs from the
  UDP source.

### Watch (replaces Graphs)

Right-click a value -> Watch. A table row repeats a GET every N seconds and shows
the value, change since the last poll, and rate per second (bits per second for
octet counters). Counter wrap and agent restart must not show false spikes.
Each watched row captures its target and Credential reference when created.
Later changes to the connection bar never retarget an existing row.

### Detect profile

On click, read `sysObjectID.0` and `sysDescr.0` from the current target with
the current Credential, then:

1. Prefer the Automatic Profile whose product MIBs define the exact
   sysObjectID registration OID (or its longest defined prefix).
2. Otherwise match on the enterprise number. Several product profiles may share
   one enterprise number (for example Extreme Fabric Engine and Switch Engine),
   so show a short choice list with sysDescr.
3. No match or a timeout changes nothing and shows a message.

Never contact the device except when the user clicks Detect. Never hard-code
vendor names or enterprise numbers. Mapping comes from the MIB collection.

### Target layout

```text
File  Edit  View  MIB  Settings  Help
Target [...]  Credential [...]  MIB Profile [...] [Detect]   (activity) [Show panel]
[Query] [MIBs] [Traps]
Query: OID navigation and collection search; MIB tree | [Results] [Watch] [Macros] [Search when open] | Inspector
MIBs:  [Library] [Profiles] [Editor]
Traps: trap list and selected trap details
[Output] [Log]                                (shared panel; collapsed initially)
MIBs additionally: [Problems (when present)]
```

Revised 2026-11-02 (owner decision). Main workspaces are permanent tabs. Library
owns collection-wide inventory, providers, validation and dependencies; Profiles
select applicability; Editor edits and verifies source. These are peer tabs inside
MIBs. Query retains the tree, Results and Inspector. Collection Search
opens a closable local Query tab beside Results and Watch. The Editor includes visible
New/Open/Save/Save As/Verify actions. The Query tree offers Watch for scalars and
columns; scalars use .0 and columns prompt for their numeric instance suffix.

A header button toggles the shared bottom panel across all workspaces. It contains
Output and Log, with conditional Problems available only in MIBs; Traps remains a
full workspace. Watch lives in Query beside Results (owner correction, 2026-11-02). Starting a query selects
Results; background results and Watch polls never navigate. Output retains its
existing text history in the panel. Problems describes MIB environment reports only.
Query failures remain in Results and Log. Explicit Problems navigation (including
the profile indicator) selects MIBs and opens its Problems page. Background reports
never navigate. MIBs remembers its panel selection independently of Query/Traps.
The panel remembers visibility, height and selected page. Running and problem
counts stay visible in the header while collapsed. Background updates never open
the panel or change workspace. Explicit View commands and the environment badge
open the requested panel page; Problems navigation also selects MIBs. Log is in
this panel for now; a future move to a top-level workspace remains open. SNMP has no connection session.

Owner visual refinement (2026-11-04): the shared connection bar and permanent
Query / MIBs / Traps navigation keep the same height in every workspace. OID
navigation and collection search sit inside Query, below that navigation. The
Get / Get Next / Get Bulk / Walk / Table / Set / Stop toolbar buttons are retired;
menu/context actions and Escape remain. This supersedes the historical toolbar
placement described in 18a. The shared panel button is named Operations.

Profiles has an **Edit profile** selector: inspecting or editing another profile
does not activate it in Query. Only the connection bar selects the active MIB
Profile. Library's Active Profile column continues to describe that active
selection. IANA catalog browsing/downloads live in a separate **Get MIBs from
IANA…** window. It opens cached data without automatic network requests and
captures a download's destination profile. Closing that window or changing
Query's profile cannot retarget a download. The old hidden Check for Updates
placeholder is retired; the IANA catalog does not provide reliable comparable
revision data. Credentials retain their unified list and independent display
name/USM identity, with name and version displayed in separate aligned columns.

The implementation and automated validation are recorded in
[visual-refresh-validation.md](visual-refresh-validation.md): 65/65 deterministic
tests, five libsmi goldens, eight ASAN checks, five strict widget leak checks,
and installed Release startup/restart at 150% scaling passed. Desktop UX review
remains with the owner.

Owner accepted the main visual refresh and approved a dialog-consistency /
smaller-window follow-up on 2026-11-04. Editor Find, Replace and Go to Line use
standard button rows; Preferences and Credentials scroll their form content with
confirmation buttons kept visible. Instance pickers have explicit Cancel and
selection states; dismissing manual instance entry must never issue a .0 query.
The existing connection model, profile ownership and credential persistence are
unchanged. Workspaces are checked at 1024 x 720 and dialogs at 640 x 480. See
[dialog-polish-validation.md](dialog-polish-validation.md) for validation status
and the new stable manual-validation artifact. Final validation passed 65/65
complete deterministic tests (all 27 tolerance scenarios and five libsmi goldens),
8/8 ASAN workflows, 5/5 strict widget leak checks and installed startup/restart at
normal and 150% scaling. Full-workflow review also fixed long Environment warning
text forcing the window wider; the complete badge now wraps inside the header.
The 2026-11-07 finishing follow-up adds empty/busy guidance, actual work-area
focus routing, discoverable keyboard navigation and safe remembered-layout checks.
It preserves normal Tab order and fixes restored windows whose lower controls were
off-screen. See [finishing validation](finish-review-validation.md). No general
compact-density preference is introduced.

## Query Macros priority and milestone boundary

Query Macros is the current authorized feature (original owner priority, 2026-11-04),
near the top of `todo.txt`. Named persistent collections describe what to read;
the connection bar supplies Target, Credential and MIB Profile when run.
Bulk-style grouped results, scalar .0 resolution and table instance choices
(all, choose at run time, fixed) are the intended direction. Unrelated indexes
remain separate and unknown numeric OIDs need explicit instances. Query Macros
is read-focused; it replaces the current temporary multiple-varbind workflow.
The menu/Help/Preferences work and design/estimate review preceded authorization.
The [agreed design](query-macros-v1-design.md) separates four working milestones.
Milestone 1 is implemented. The owner authorized milestone 2 on 2026-11-08:
captured scalar/exact-instance runs, grouped Results, Stop and request logging.
Milestone 3 (table groups: all, fixed and choose-when-run instances) was
implemented on 2026-11-10; see [milestone 3](query-macros-milestone-3.md).
Legacy-dialog retirement remains milestone 4. Existing single-object SET remains.

## Build order

Every step ends with an app that builds, passes the deterministic suite and the
five libsmi goldens, and is runnable for manual validation. No step leaves the
application half-dismantled. Steps that exceed the 50-file guardrail are split
into smaller steps, each with its own working build.

### Phase 0: Clean baseline

1. **Record the new direction.** This document and the AGENTS.md updates. *Done.*
2. **Move to a local Linux working copy and normalize line endings.** Clone to
   `~/src/snmpb-next` on the VM's disk. Carry over all uncommitted work with
   line-ending-only differences removed. Add `.gitattributes`. Create the `nas`
   backup bare repository. Establish the first Linux build and test baseline.
   Leave the old NAS working tree untouched. Do not change committed MIB file
   bytes (see Known issues). Prompt: `docs/prompts/step-02-local-linux-working-copy.md`.
   *Done (2026-10-30): the `nas` backup repository could not be created over
   the gvfs mount (chmod unsupported). The NAS share is now kernel-mounted at
   `/mnt/nas` (see AGENTS.md). The bare repository
   `/mnt/nas/Personal/Software/git/snmpb-next.git` holds `main`,
   `modernize/qt6-cmake`, and tag `v1.0`, identical to the local refs.*
3. **Finish or set aside in-progress work.** The uncommitted
   `startupcoordinator.*` and `workbenchshell.*` files and related edits. The
   user decides; the user commits. *Done (2026-10-30): kept, with the MIB
   tolerance and Problems work, as the baseline.*
4. **Mark the baseline.** The user commits or tags a known-good copy (for
   example `before-redesign`). *Done (2026-10-30): commit `0554781`, tag
   `before-redesign`. Debug and Release: 57/57 tests, 5/5 libsmi goldens.*

### Phase 1: Removals

5. Remove Graphs and the vendored Qwt target. *Done (2026-10-30), in two parts
   because Qwt is 2,837 tracked files, far over the 50-file guardrail.* **5a:**
   Graphs removed from the application (22 files deleted), the Qwt build target
   and option removed, and the dead graph-era `Agent::GetSyncValue` removed.
   **5b:** after the owner explicitly approved the file count, the `qwt/` and
   `third_party/qwt-6.3.0/` directories (2,837 files) were deleted, along with
   the Qwt rules in the legacy qmake `Makefile` and `snmpb.pro`, the Qwt entry in
   `docs/third-party-dependencies.md`, and the Qwt notes in `readme.txt`.
6. Remove Discovery. *Done (2026-10-30): 8 files deleted, the Discovery tab and
   its settings controls removed, the Devices activity opens the Tree until the
   layout step replaces it, and the unused `DeviceTreePlacementService`
   (only Discovery called it) removed. Saved Discovery settings are left in the
   user's settings file untouched.*

### Phase 2: Layout and connection model

7. Main window layout per the target layout, building on the workbench shell.
   *Implemented; the final workspace arrangement is recorded under Target layout
   and steps 18a–18e below.*
8. Replace the address book with the connection bar in a **single working
   step**: Target history, Credential dropdown, MIB Profile dropdown, the
   Credentials screen, and global connection settings. Graph-era and
   Discovery-era dependencies on Agent Profile IDs must be gone by the end of
   this step.
8a. *Done (2026-10-31): the connection bar drives requests.* Target (typed, with
   a 30-entry recent history that remembers the last Credential per target, never
   the MIB Profile), Credential (each saved community appears as an SNMPv2c and an
   SNMPv1 entry; each saved SNMPv3 user once) with a Manage button, and the single
   MIB Profile selector, all in the header. `Agent::ResolveCurrentSelection` builds
   the request from the bar (`connectionmodel.*`); timeout, retries, and GETBULK
   settings come from the global preferences. A credential that is deleted while
   chosen leaves the selection empty rather than switching silently. The Devices
   activity is no longer offered. Port is typed as `host:port`; without one, the
   global "Default SNMP port" in Preferences applies (161). Covered by
   `snmpb.connection-model` and by the `live-get-through-connection-bar` scenario,
   which runs a real Get against `tools/loopback_snmp_agent.py` (127.0.0.1 only).
8b-1. *Done (2026-10-31):* deleted the Device pane, device tree (model, repository,
   details editor), and profile import/export (16 files, their 4 tests, the Devices
   dock, the retired header labels, and the Devices activity). Saved settings that
   pointed at the Devices activity open MIBs (`workbench/activity` values keep their
   meaning: 0 MIBs, 2 Queries, 3 Events). `device-tree.conf` and `agents.conf` are
   left on disk untouched.
8b-2. *Done (2026-10-31):* removed the Agent Profile manager dialog, the hidden profile
   combo and protocol radios and their request path in `Agent`, the saved-profile
   service, repository and operations, per-device metadata, profile-to-credential
   bindings, the preferred-MIB resolver, and the profile-based request settings
   (about 45 files including tests). `agents.conf`, `profile-metadata.conf`,
   `credential-bindings.conf` and `device-tree.conf` are neither read nor created and
   are left on disk untouched (`snmpb.launch-smoke` asserts a fresh configuration
   contains none of them). The credential services no longer track what references
   them: deleting a credential is always safe because the connection bar then shows
   "choose a credential". Kept: `AgentProfileRecord` (now in `agentprofilerecord.h`)
   is still the one-request settings struct the SNMP layer is built from.
8c. *Done (2026-10-31):* `AgentProfileRecord` is now `SnmpRequestSettings`
   (`snmprequestsettings.h`), the compatibility shim is gone, and the unused "selected
   Agent Profile" preference fields are no longer read or written (the saved keys stay
   in the settings file untouched).
8d. *Done (2026-10-31):* one **Credentials** screen (Options > Credentials, or Manage next to
   the Credential list) with an SNMPv1/v2c communities tab and an SNMPv3 users tab and a single
   OK and Cancel. OK saves the SNMPv3 users first (validation and rollback), then the
   communities; pressing OK with no edits leaves every file byte-identical (checked against a
   copy of a real saved user). Manage opens on the tab matching the chosen credential.

### Phase 3: Features, in dependency order

9. **Raw vs interpreted values with mismatch warnings.** Show the received
   ASN.1 type and raw value beside the MIB interpretation (named values,
   DISPLAY-HINT, units). Warn on wrong type, undefined enumeration value,
   out-of-range value, and size violations. Used by Results, Watch, Traps, and
   Tables. *Results done (2026-10-31); Results now sit directly under the Tree (not in the bottom panel), with a one-line status above the table that says how the last request ended: "Finished: 37 objects in 4 requests", a timeout, or, for SNMPv3, a plain-English explanation of a refused request (`snmpreport.*`, covered by `snmpb.snmp-report`). Before this change an SNMPv3 REPORT was shown as if it were a result and errors appeared only on the Raw tab.* The Results table has Received (type and
   raw value) and Warnings columns, and the row is highlighted on a mismatch. The
   check is the pure function `CheckMibValue` (`mibvaluesemantics.*`, covered by
   `snmpb.mib-value-check`) and is exercised end to end against a loopback agent
   that answers one object with the wrong type. Query Tables, Traps and Watch now
   use the shared renderer/checker too (steps 11, 17 and 18b). Broader DISPLAY-HINT
   validation and TEXTUAL-CONVENTION constraints beyond range, size and enumeration
   remain outside the implemented checks.
10. **Watch.** *Done (2026-10-31):* right-click a row in Results and choose "Watch this value".
    The Watch tab (now inside Query) lists each watch with its own target and credential, value,
    change since the previous poll, rate per second, the raw received type, any MIB warnings,
    and its status. Rates use the device's own sysUpTime when it answers (so polling jitter does
    not distort them) and fall back to the local clock; "octets" objects show bits per second.
    A falling sysUpTime is a device restart and resets the baseline. A falling Counter32 is a
    wrap only when the previous value was in the top quarter of the range, with or without
    sysUpTime (a counter cleared on a running device is a reset, not a wrap), so none of these
    shows a false spike. A row captures the target text and a credential
    reference; the credential is re-resolved at every poll, and the connection bar never
    retargets a row (`snmpb.watch-service`, `snmpb.watch-calculator`, and the loopback
    scenario, which points the bar elsewhere while a watch keeps polling). Watch now
    starts from both Tree and Results and lives beside Results inside Query (18b).
    Follow-up (2026-11-07): definitions save atomically without secrets or results,
    restore paused, and support per-row interval editing. New Watch rows capture
    the resolved port and context as well as their credential reference. Corrupt/
    newer stores and external edits are preserved with a visible error. Column/
    table Watch expansion remains a separate deferred design.
11. **Traps**: Copy source IP, plus raw/interpreted varbinds from step 9.
    Implemented (2026-11-03): typed SNMP++ value copies retain exact binary data
    and Counter64 precision; the shared MIB renderer/checker supplies interpreted
    and raw values, received types, units and mismatch warnings. Traps stays a
    top-level workspace. Active Profile publication reinterprets retained history;
    incremental receipt preserves selection by record ID and eviction clears stale
    details. Copy source IP preserves the v1 relayed-agent rule. Saving the history
    limit now updates the running service immediately. Validation also exposed
    a narrow Library/Profile publication timing margin; batching painting and
    column sizing preserves selection/scroll and keeps the original UI budget.
    No parser, transport,
    notification-security or settings-format change.
    Validation: focused 15/15; 47 widget assertions in Debug/ASAN; complete
    deterministic suite 65/65 (946.59 seconds), including all 27 application
    scenarios and five libsmi goldens; installed Release startup/restart, binary
    and 355 MIB/PIB comparisons, release-tree verification, ldd and diff checks
    passed. Unfiltered sanitizer setup finds existing libsmi leaks (the pipeline's
    exact count reproduces without trap code). Components pass with explicit
    parser-only leak exclusions; full-app ASAN invalid-access checks pass with
    existing full-app leak detection disabled. Existing warnings and Windows/macOS
    CI remain. No desktop GUI, physical devices, commits or pushes. Twenty-two task
    files; 30 total with prior uncommitted MIB work; no scope guardrail triggered.
    Install: `/tmp/mib-navigator-manual-validation/traps`. See
    [Traps validation](traps-validation.md) for ownership, limitations and exact
    build/executable paths. Desktop UX review remains.
12. **Log / PDU view**, starting with plain-English SNMPv3 REPORT translation
    (unknown user, wrong auth, decryption failure, time window, unknown engine
    ID). Full packet decode is optional later. *Done (2026-10-31):* the REPORT
    translation (`snmpreport.*`) covers all six usmStats counters and the snmpMPD
    counters, and appears in the status line above Results, the Watch row, and the
    Log. The **Log** tab (bottom panel) records every request this session: time,
    operation, target, identity (for example "SNMPv3 synoptics (authPriv)"), the
    object, how it ended (OK, Failed, Timeout, Refused by device, Stopped), how many
    objects, how long it took, and the plain-English detail. It never contains a
    community or passphrase, can be copied as tab-separated text for a ticket, and
    keeps the newest 1000 entries. Table and instance-picker requests are logged
    (step 17). Watch polls and raw packet decoding remain outside this log view.
13. **MIB Problems view** (competitive features #1 and #6 merged). Actionable
    messages for missing imports, unresolved symbols, duplicate and conflicting
    providers, and parse diagnostics. Show where each definition came from and
    why that provider was chosen. *Foundation done (2026-10-30): tolerant
    loading, a structured problem report with parser line numbers, and a
    Problems tab (see `mib-architecture.md`). Provider conflict reports now name
    candidates, revisions and the chosen provider (18a-4); Problems is MIB-only
    (18b). Follow-up (2026-11-07): text/severity filters, semantic severity sorting
    and CSV copy/export operate on the published report. Exports snapshot the
    visible sort/filter before the file chooser and retain full source details.*
14. **Collection-wide search** (#7) by name, qualified name, or numeric OID
    across the whole MIB collection, not only the loaded tree. Results say which
    module and which profiles contain the match. *Done (2026-10-31):* the "Search all
    MIBs..." box in the Query search field opens a local Search tab beside Results and Watch. It searches an
    index built by reading the text of every MIB file in the Library (`mibcollectionindex.*`),
    so it covers MIBs that are in no profile and even MIBs the parser rejects. Queries: a
    name or part of one, `Module::name`, or a numeric OID (an instance finds the definition
    above it). Each result shows kind, module, OID, and the profiles that contain the module;
    rows in the active profile are green. Double-click opens the Tree if the module is active,
    otherwise the editor at the definition. The index rebuilds in the background when the
    Library changes (`mibcollectionservice.*`), reusing unchanged files. An OID it cannot work
    out (a missing parent) is shown as unresolved, never guessed. Checked against libsmi on
    every bundled standard MIB (25,678 nodes, 0 wrong) and, optionally, a real vendor
    collection (40,602 nodes, 0 wrong; set `SNMPB_ACCURACY_EXTRA_DIRS`). Building the index
    for 799 files takes about 8 seconds in a Debug build over a network share, which is why it
    runs in the background; step 15 (persisting it) is not needed yet.
15. **Persist compiled MIB data** (#9), only if measured search or startup
    performance requires it.
15a. **Automatic Profiles.** *Done (2026-10-31):* every `Vendor/Product` folder of the Library is a
    profile, named "Vendor / Product", with the stable id `auto:Vendor/Product`. Its members are the
    MIB files in that folder at any depth; it looks in the product folder first, then the vendor
    folder (files its products share), then the Library's Standards folder for imports. `Standards`
    and `Unassigned` at the first level are reserved and never become profiles; files loose in the
    Library or directly in a vendor folder belong to no product. They are derived, never saved,
    cannot be renamed, deleted or edited member by member, and follow the folder when the Library
    refreshes (`MibBuildAutomaticProfiles`, `snmpb.mib-automatic-profile`, and the end-to-end
    scenario). Profiles made by hand are untouched and still work alongside them. Detect profile
    prefers an Automatic profile over a hand-made one that holds the same MIBs.
16. **Detect profile.** Reuses the collection-wide index from step 14. *Done (2026-10-31):*
    the **Detect** button next to the MIB Profile selector sends one GET for sysObjectID.0 and
    sysDescr.0 (only when clicked, never otherwise), then matches with
    `MatchProfileForSysObjectId` (`profilematch.*`): the longest registered prefix of the
    sysObjectID that some MIB in the collection defines (stopping one level above the
    enterprise number), then, failing that, every profile holding any MIB the vendor registers
    under its enterprise number. One candidate profile is selected at once (the status bar and
    the Log say which and why); several offer a short choice list showing sysDescr; none changes
    nothing and explains why (for example "the collection has MIBs for this vendor but no saved
    profile includes them"). A device that is unreachable, refuses the login (SNMPv3 reports are
    translated), or reports no sysObjectID changes nothing. No vendor name or enterprise number
    is built in. Automatic Profiles now exist (15a) and Detect prefers them. The
    built-in Standards profile is excluded because it would match every device.
17. **Table verification** (#3) against real vendor tables (sparse rows,
    string/IP indexes, IMPLIED, AUGMENTS), plus Copy as CSV. *Done (2026-10-31):* Query Table
    reads every column in step (`SnmpTableOperation`): the smallest next instance across the
    columns is the next row, so a row missing its first column (a sparse table) is no longer
    lost, and a dense table costs one request per row instead of one per cell (up to 8 columns
    per request; GETBULK on SNMPv2c/v3 reads many rows per request, GETNEXT with noSuchName
    handling on SNMPv1; a "tooBig" answer makes it ask for less). Instances sort numerically.
    A device that returns an OID that is not after the previous one ends the walk with a plain
    reason instead of looping; row and request caps stop a runaway table. An SNMPv3 REPORT
    (wrong passphrase and so on) is now reported as a refusal rather than as an empty table
    (the same fix applies to the instance list). Not-accessible columns (usually the INDEX) are
    not requested; the **Index** column shows the INDEX values decoded from the instance
    (`tableindex.*`: integers, strings with or without a length prefix, fixed-size strings such
    as MAC addresses, IpAddress, object identifiers, IMPLIED, and AUGMENTS/SPARSE/EXPAND rows
    following the row that declares the index; an instance that does not fit shows the raw
    instance only). Right-click in Results: **Copy table as CSV** (the last Query Table, empty
    cells where a row has no value) and **Copy results as CSV** (what is on screen, in the order
    shown); both are RFC 4180 text. Table and instance-list requests now appear in the Log, and
    a failed instance list says why instead of doing nothing. Also fixed: SNMP++ returns a
    device's SNMP error status (noSuchName, tooBig, ...) as a positive code, which the transport
    classified as "no response". Tests: `snmpb.snmp-table` (fake in-memory agent: dense, sparse,
    SNMPv1 and v2c, GETBULK efficiency, chunking and tooBig, stuck agent, timeout, cancel, a
    300-case random sparse table compared with the truth, index decoding, CSV, SNMPv3 REPORT) and
    the loopback scenarios `live-table-*` (real SNMP to a loopback-only agent that now answers
    real GETBULK and SNMPv1 noSuchName: sparse table, AUGMENTS, IMPLIED), each also compared
    object for object with a plain Walk of the same subtree. Checked read-only against real
    Fabric Engine tables on 172.16.50.32 (ifTable 42 rows, ifXTable, entPhysical, rcVlan with 11
    sparse rows, Q-BRIDGE FDB with MAC indexes, ipNetToMedia with IP indexes, dot3Stats and
    others): every table matched a plain Walk of the same subtree exactly.

    Added at owner request (2026-11-03; ready for manual validation): table and entry
    context menus now offer Query Table > All Instances / Select Instance.
    Discovery takes the union of readable columns so sparse rows remain selectable,
    sorts/deduplicates numeric suffixes, and shows decoded INDEX values. Selecting
    a row performs batched exact GETs through the existing table worker and Results
    grid; missing cells remain empty and a vanished row cannot become its successor.
    Discovery and retrieval share a captured target/credential/MIB environment.
    Cancel and Stop preserve previous results during selection. No new persistence
    or alternate MIB ownership is introduced. Discovery enumerates readable columns
    first, so large tables may take time; Stop can cancel that work.
    Validation: full Debug build, focused 9/9, complete deterministic suite 64/64
    (888.51 s; all 26 tolerance scenarios and five libsmi goldens), and two backend
    ASAN targets with leak detection passed. Installed Release repeated the three
    table scenarios (v1, v2c, AUGMENTS/IMPLIED), including actual parent-table and
    entry menus, decoded sparse instance lists, exact first/last rows, captured
    connection settings, Cancel and Stop. Release install/startup/restart,
    release-tree verification, executable and 355 module byte checks, ldd (no
    missing libraries), and diff whitespace checks passed. Review also fixed
    repeated-OID discovery loops, rejected-picker queries and replacement of a
    worker thread before queued cleanup; dedicated regressions cover these.
    One intermediate ASAN run used mismatched incremental objects after a header
    edit; a rebuild of affected targets resolved that build artifact failure.
    Existing Qt deprecation/tab-order/setuid warnings and documented legacy
    full-app leaks remain outside this change. No desktop GUI or real devices
    were used. Windows/macOS require CI. Sixteen task files changed, no scope
    guardrail triggered, earlier work preserved, nothing committed/pushed.
    Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`; Release build:
    `/home/synoptics/src/snmpb-next/build-linux-release`; install:
    `/tmp/mib-navigator-manual-validation/table-row`; executable:
    `/tmp/mib-navigator-manual-validation/table-row/bin/mib-navigator`.


    Follow-up correction (2026-11-03; owner verified latest build works as expected): discovery progress had
    incorrectly used the completed-empty-reply formatter, showing "The device
    returned no objects" while discovery was still running. Progress and picker
    cancellation now avoid empty-response claims and do not finish the previous
    Results query. Discovery's per-cell GETNEXT round trips are replaced by the
    existing sparse-aware table engine: batched GETBULK for v2c/v3 and batched
    GETNEXT for v1, with shared tooBig fallback, bounds, cancellation and REPORT
    handling. An instances-only plan retains suffixes without collecting cells.
    A deterministic 240-row/24-column sparse fixture reproduced 5,664 requests
    before the fix; afterwards v2c/v3 take 30 requests and v1 takes 724, retaining
    all 240 suffixes. The request-budget regression failed before the change and
    passes afterwards. The repeated-OID test permits the legitimate extra v1
    noSuchName batch retry while still requiring bounded failure.
    Validation: full Debug build, focused 9/9, complete suite 64/64 (863.65 s;
    26 tolerance scenarios, five libsmi goldens), two backend ASAN targets with
    leak detection, and installed v1/v2c/AUGMENTS/IMPLIED scenarios passed. Actual
    menu workflow checks cover the exact progress message, successful selection,
    no false empty notice on Cancel, and Stop preserving prior results. Release
    install/startup/restart, release-tree verification, executable and 355 bundled
    module byte comparisons, ldd (no missing libraries), and diff checks passed.
    No new persistent state or MIB ownership was added; discovery reuses the table
    engine and captured request environment. Existing Qt/setuid warnings and
    documented legacy full-app leaks remain. Device-specific elapsed time still
    was confirmed by the owner on 2026-11-03; automated validation used no physical
    devices or desktop GUI.
    Windows/macOS need CI. Nine task files changed without triggering a scope
    guardrail; earlier work remains intact, nothing committed/pushed. Debug build:
    `/home/synoptics/src/snmpb-next/build-linux-debug`; Release build:
    `/home/synoptics/src/snmpb-next/build-linux-release`; install:
    `/tmp/mib-navigator-manual-validation/row-discovery`; executable:
    `/tmp/mib-navigator-manual-validation/row-discovery/bin/mib-navigator`.

### Phase 4: Finish

18. Visual polish: icons, consistent wording, optional dark mode, keyboard
    shortcuts, remembered window layout. Refined on 2026-11-01 by a UI review
    into the steps below, one build step each, in this order.
18a. **Layout.** Remove the activity rail and its sidebar (the Queries and
    Events sidebars held one button each, and the MIBs sidebar had two entries,
    Modules and Catalog, that opened the same page). The MIB tree with its
    filter becomes the permanent left pane. The query toolbar and the OID field
    span the window under the connection bar. The middle area shows its tab bar:
    Results always, plus closable MIB Library, MIB Editor and Search tabs. The
    bottom panel holds Watch, Traps, Log and Problems; Raw stays there until 18b
    gives Table results a real grid. The SNMP library log (the old Diagnostics
    tab) moves to Help > Diagnostics. A View menu opens the Library, Profiles and
    Editor and shows or hides the panels until 18d reorganises the menus. Delete
    the unreachable legacy "MIB Library" tab from `mainw.ui` (and the code that
    kept updating its hidden labels). Fix the Ctrl+R clash between Verify MIB
    syntax and Replace (Replace becomes Ctrl+H). The "results below or beside the
    tree" preference no longer applies and is hidden; its saved value is left
    untouched, as are the retired `workbench/activity` and
    `workbench/content-splitter` keys. *Done (2026-11-01).*
18a-2. **Output in the middle, Results at the bottom** (owner request after 18a).
    The middle area's always-open tab is Output, the SnmpB-style text that the
    old "Raw" tab held (renamed: it is interpreted text, not raw packets, and
    "Raw" is kept for the received-value toggle in 18b), with a Clear button.
    Results becomes the first bottom tab; its title counts rows with MIB
    warnings ("Results (2 warnings)"); it does not take the tab forward by itself. The
    bottom panel spans the full width and the Inspector stops above it. Also
    fixes the Library's "All states" filter, which was never placed in a layout
    and sat over the Library/Profiles tab names. *Done (2026-11-01).*
18a-3. **MIB Library as a full page, folder-based Profiles** (owner request).
    View > MIB Library replaces the whole working area: no tree, bottom panel or
    Inspector while it is shown, and leaving it restores them as they were. On
    the Profiles page the available side is the Library's folder tree
    (Standards, Unassigned, Vendor/Product, ...) with the MIB files inside;
    Add and Remove handle both folders and single files, so "Add Files..." and
    "Add Folder Snapshot..." go away. A folder added to a Custom profile is
    remembered as a folder and followed, like an Automatic profile follows its
    folder: MIBs later added to or removed from it change the profile (owner
    decision, 2026-11-01). This adds folder members to the saved profile format;
    the change must be additive, existing profiles load unchanged, and an older
    build reading the file must not lose data.
    Problems moves from the bottom panel to a third tab of the MIB Library page
    (Library | Profiles | Problems), headed with the active profile's name,
    because every item on it comes from loading the active profile's MIB
    environment (nothing is about SNMP traffic, devices or traps). The
    environment badge in the main window ("Loaded with 3 problems") opens that
    tab, so the cause of a missing tree branch or an undecoded value is one
    click away. The bottom panel keeps Results, Watch, Traps and Log (owner
    decision, 2026-11-01).
    The MIB Editor also takes the full page (owner decision, 2026-11-01).
    Leaving either full page (its tab's close button, or View > Output) brings
    the tree, Inspector and bottom panel back as they were. Inside a followed
    folder, Remove on a single file excludes that file; the profile follows the
    folder except its excluded files, and the member list shows them as
    excluded so the difference from the folder is visible (owner decision,
    2026-11-01). Defaults: a followed folder includes its subfolders (as
    Automatic Profiles do); existing Custom profiles made with "Add Folder
    Snapshot" keep their exact files and are not converted; a followed folder
    that is renamed or deleted shows as missing in the profile and on the
    Problems tab, never silently dropped.
    *Done (2026-11-01).* Followed folders live in `profile-folders-v1.json` next to
    `profiles-v1.json`, which keeps exactly the format older builds read and rewrite
    (`snmpb.mib-profile-folders` simulates an older build rewriting it). Every
    profile becomes a plan through one function, `MibPlanForProfile`, used by
    activation, start-up, Detect and collection search alike (start-up used to
    have its own copy). A followed folder's MIB files come from the Library
    index, the same list the plan uses. A missing followed folder is the plan
    finding `FolderNotFound`, shown on Problems as "Followed folder not found".
    The report/cache limitation originally recorded here is fixed in 18a-4: missing-folder
    findings participate in the Environment cache key even when the folder held no MIB files.

18a-4. **Problems at the bottom when there are problems; clear, revision-based
    conflicts** (owner feedback after 18a-3, 2026-11-01). Problems leaves the MIB
    Library page and is a bottom-panel tab again, shown only while the active
    profile has errors or warnings, titled with their number ("Problems (1)").
    The environment badge and View > Problems open it on demand (also when there
    are only notes). When different files define the same module, the one with
    the newest LAST-UPDATED revision is used; only when revisions are equal or
    missing does the first by path win (previously always first by path, which
    chose correctly in the owner's Fabric Engine folder only by luck). The
    Problems entry names the module, each file relative to the Library with its
    revision date, and which one is used; the Profiles summary names conflicting
    modules instead of "1 conflicts". Also: the test harness wrote Library
    index files into the user's real data folder; tests must keep every file
    inside their own temporary folders. The owner's library was checked before
    any file move: `rfc2851.mib` stays where it is because the Custom profile
    "Extreme Fabric" lists it as an exact file.
    *Done (2026-11-01); manual UX validation pending.* Problems is a conditional bottom tab;
    its count comes from the active Environment report, and its automatic appearance and count
    updates do not take focus. The badge and View > Problems can open it even for notes alone, returning
    from a full page to Output when necessary. The Library has only Library and Profiles tabs.
    Both selected-file conflicts and imported dependencies use the newest per-module LAST-UPDATED
    within the existing winning scope; an explicit dependency pin still wins. Equal or missing
    dates fall back to path order; two-digit years mean 19YY. Identical copies are harmless.
    Reports name every candidate relative to the Library, with its revision and the selected file;
    the Profiles summary names conflicting modules. Resolver policy 2 invalidates old caches.
    Review also fixed stale conflict reports after an alternative provider changes or disappears:
    findings and candidate hashes now participate in the Environment key. Findings are retained
    by module and kind, so a Modified note or stale-pin warning cannot hide a conflict warning.
    Modified notes describe only the selected provider, never a shadowed copy.
    This also fixes the
    missing-empty-folder cache limitation from 18a-3. Indexes and diagnostic logs honour isolated
    state roots; existing user-state files and the owner's exact rfc2851.mib membership are untouched.
    Regression checks cover selected/imported conflicts, ties, missing/invalid dates, two-digit
    years, multi-module files, quoted/comment dates, changed/removed alternative providers, missing
    empty folders, conditional tabs, full-page badge navigation and test-state isolation.
    Validation: focused tests 5/5; complete Debug suite 60/60 (784.76 seconds), including all five
    libsmi goldens and 26 tolerance scenarios; AddressSanitizer resolver and shell checks passed;
    `git diff --check` clean. Release application build and install passed; the executable and all
    355 bundled MIB/PIB files match the build, and `ldd` reports no missing libraries. Existing Qt
    deprecation and unchecked `setuid` warnings remain. An earlier execution-session termination
    was avoided by running the final suite in a persistent process; the final run had no failures
    or timeouts. No desktop GUI or real device was used. Windows/macOS still need CI verification.

18a-5. **Permanent workspace navigation and shared collapsible panel** (owner
    decision, 2026-11-02). Supersedes earlier layout details: Query | MIBs | Traps |
    Watch are permanent top-level tabs; MIBs contains Library | Profiles | Editor.
    Results | Log | Problems share a bottom panel, collapsed on first use, with a
    Show panel / Hide panel button and running/problem indicators in the header.
    Its selected page, height and expanded state persist using new presentation
    keys; old settings remain untouched. Background updates do not expand or
    navigate; explicit Problems navigation stays in the current workspace.
    Reparent existing widgets without changing request, trap, watch or MIB runtime
    ownership. Inspector and query tools belong to Query. This layout milestone is
    implemented before 18b's Results grid work; manual desktop UX review remains.
    Review fixed restoration of a saved Problems selection: wait for the initial
    report, cancel that pending preference on explicit navigation, and never expand
    the panel automatically. The loopback Watch workflow confirms continued polling
    while its workspace and the panel are hidden, with its original target retained.
    The scenario expectation was updated to match this strengthened check's message.
    Validation: focused checks 3/3; complete Debug suite 60/60 (832.38 seconds),
    including all five libsmi goldens and 26 tolerance scenarios; AddressSanitizer
    shell/lifetime checks passed; `git diff --check` clean. Release build/install
    passed; executable and all 355 bundled MIB/PIB files match, with no missing
    libraries reported by `ldd`. Existing Qt deprecation and unchecked `setuid`
    warnings remain. The final suite had no failures or timeouts. No desktop GUI
    or physical device was used; Windows/macOS still require CI verification.
    Nine files changed in this milestone, preserving the existing uncommitted work;
    no file-scope guardrail was triggered. No commits or pushes were performed.

18a-6. **Editor actions and Query Watch workflow** (owner correction, 2026-11-02).
    Add visible Editor file actions using the existing menu actions. Restore Watch
    to the Query tree context menu; ask for a table column's instance, preserve the
    captured target/credential, and show watch rows beside the tree in Query's
    center pane. Output and Watch are permanent local tabs; Search remains optional.
    The separate top-level Watch tab is replaced by this Query view. Existing
    watches and polling ownership stay in the single Watch service.
    Implemented. Regression workflows click the Editor Open/Verify buttons and the
    Query tree context menu, poll scalar .0 and compound column instances, verify
    that results appear beside the tree, and check cancellation and invalid input.
    Review added explicit 32-bit component bounds: malformed or oversized instance
    suffixes are explained and never create a watch or reach the transport.
    Validation: focused tests 4/4 plus the targeted loopback scenario; complete Debug
    suite 60/60 (833.91 seconds), including 26 tolerance scenarios and all five
    libsmi goldens; AddressSanitizer shell/lifetime check passed; `git diff --check`
    clean. Release build/install passed. The installed executable and all 355
    bundled MIB/PIB files match the build; `ldd` reports no missing libraries.
    Existing Qt deprecation and unchecked `setuid` warnings remain. Final tests
    had no failures or timeouts. Desktop UX review and Windows/macOS CI remain;
    no desktop GUI or physical device was used. Ten source/doc files changed,
    preserving existing work, with no scope guardrail triggered. No commits/pushes.

18a-7. **Unified named credentials and same-user SNMPv3 variants** (owner request,
    2026-11-02). Replace the communities/users tabs with one list and New/Duplicate/
    Delete above it. New chooses v1, v2c or v3; a matching editor appears on the right.
    SNMPv3 display names are independent of the wire USM username. Saved credential
    IDs select complete protocol/secret snapshots; repeated wire usernames are valid.
    Each v3 variant has an isolated, reusable SNMP++ message/security context so
    concurrent operations cannot overwrite another variant's USM user or caches.
    Incoming v3 notifications retain the first saved variant per username as their
    default; a successful query installs the exact variant for that remote engine ID.
    An unsolicited trap from another variant's device therefore needs a successful
    query first in the current session. Existing single-variant trap behavior stays.
    Legacy USM files and identity mappings remain untouched during migration. New
    v3 records use an atomic, owner-restricted store next to the old identity file;
    existing IDs are retained and imported unmapped IDs are deterministic. Existing
    community entries keep their legacy shared v1/v2c behavior and history keys;
    newly created entries select one version. Cancel discards working copies. Save
    publishes neither service until both atomic files succeed and rolls back the
    first file if the second write fails. Corrupt stores are reported, never replaced.

    Validation: focused workflows 5/5, credential AddressSanitizer/leak checks 4/4,
    revised Walk interleaving check 10/10 consecutive runs, and the complete Debug
    suite 62/62 (839.95 seconds), including all 26 tolerance scenarios and five
    libsmi goldens. Autonomous review fixed the SNMP++ None protocol-ID mismatch,
    security-cache destruction order, and learned notification keys surviving
    deletion of a secondary variant. Release build/install, installed headless
    startup/restart, executable/MIB byte comparisons (355 MIB/PIB files), and `ldd`
    passed. `git diff --check` is clean. Existing Qt deprecation and unchecked
    `setuid` warnings remain. The broader app passes ASAN invalid-access checks
    with leak detection disabled; strict full-app LeakSanitizer separately reports
    pre-existing libsmi diagnostic and legacy UI leaks (101,206 bytes), documented
    in `usm-credential-management-v2.md`. Those are not a leak-free full-app result.
    Desktop UX and Windows/macOS CI remain. No desktop GUI or physical device was
    used. This request changed 35 source/doc files while preserving prior work;
    no file-scope guardrail triggered, and nothing was committed or pushed.

18b. **Results.** Table queries show a real grid (rows are instances, columns
    are the table's columns, plus Index), with warning cells tinted and
    right-click Watch and Copy as CSV. Get and Walk results default to
    Object.Instance, Value and Type; OID, Syntax, Received and Time are available
    from the header's column menu. Warnings show as an icon and row tint with the
    raw value in the tooltip, and an Interpreted/Raw toggle. A header line above
    Results names the object, its OID, the row count and the time taken; a filter
    box narrows the rows. Results occupies the main Query area beside Watch; Output
    is in the bottom panel (owner correction after Results review, 2026-11-02).

    Implemented (2026-11-02): `QueryResultsPanel` owns presentation snapshots copied
    from query results, never parser pointers or retained model indexes. Table results
    show one row per instance with decoded Index and readable data columns; unavailable
    cells have no Watch action and export as empty values. Get/Walk default to
    Object.Instance, Value and received Type; the header menu exposes metadata columns.
    Filter, numeric sorting (including full Counter64 precision), Raw/Interpreted,
    warning icons/tints/raw tooltips, and object/OID/count/duration summaries apply to
    the displayed snapshot. CSV copies visible rows and columns in displayed order
    and value mode. The old table-record snapshot remains for independent verification;
    it no longer owns clipboard exports. Warnings are counted incrementally and streamed
    Walk delivery remains bounded. Query entry points exclude overlapping result
    operations, including the interval before a table completion is delivered. Results
    now occupies the main Query area; Output keeps its existing text representation
    in the shared bottom panel.
    Expanded table/all-instance Watch remains deferred in `todo.txt`.
    Validation: focused tests 9/9; complete deterministic Debug suite 63/63
    (822.90 seconds), including 26 tolerance scenarios and all five libsmi goldens.
    Results component AddressSanitizer/leak checks passed. Application startup/restart
    AddressSanitizer invalid-access checks passed with leak detection disabled for
    the existing application leaks recorded in step 18a. Release build/install,
    installed startup/restart and synthetic loopback table workflow passed; executable
    and all 355 bundled MIB/PIB files matched the build, and ldd found no missing
    libraries. Diff whitespace checks passed. Existing Qt deprecation/setuid warnings
    remain; Windows/macOS still require CI. This milestone changed 12 files without
    triggering scope guardrails. No desktop GUI or physical devices were used, and
    no commits or pushes were made.
    Desktop UX validation is pending.

    Layout correction (2026-11-02): Results | Watch are permanent Query tabs;
    Output | Log | conditional Problems are panel pages. A named query start selects
    Results once, while streamed results and Watch updates preserve navigation.
    Existing Results/Output widgets are reparented; request and Watch ownership stay
    unchanged. Old saved panel ID "results" maps to "output" during restoration;
    normal save writes the new ID. Warning counts annotate the main Results tab.
    MIB-only Problems placement remains a separate next step.
    Validation: focused tests 8/8; complete Debug suite 63/63 (864.75 seconds),
    including all 26 tolerance scenarios and five libsmi goldens. Results and shell
    AddressSanitizer checks passed 2/2 with leak detection enabled. Installed Release
    startup/restart and synthetic table workflows passed, including starting from
    Watch and preserving Watch selection through background completion. The Release
    executable and all 355 installed MIB/PIB files match the build; ldd found no missing
    libraries. Diff whitespace and line-ending checks passed. No new product defects
    were found. Existing Qt deprecation/unchecked setuid warnings and previously
    recorded application leaks remain outside this layout step; Windows/macOS need
    CI. Nine files changed with no scope guardrail triggered. No desktop GUI or
    physical device was used; no commits or pushes were made. Desktop UX review is
    pending. Release build: `/home/synoptics/src/snmpb-next/build-linux-release`;
    install: `/tmp/mib-navigator-manual-validation/query-layout`;
    executable: `/tmp/mib-navigator-manual-validation/query-layout/bin/mib-navigator`.
    Owner verified Watch behavior after the layout correction (2026-11-02).

    MIB-only Problems (2026-11-02): show Problems only in the MIBs workspace,
    conditional on the MIB report or explicit inspection. Profile indicator and View
    Problems navigation select MIBs without changing its Library/Profiles/Editor tab.
    Background reports never navigate or expand the panel. MIBs remembers its selected
    panel page separately from Query/Traps; presentation hides cannot overwrite that
    preference. Old shared Problems preferences restore safely into the MIB-only
    selection, with settings rewritten only during normal save. Query errors and
    value warnings remain in Results/Log; MIB report ownership stays unchanged.
    Validation: focused tests 8/8; complete Debug suite 63/63 (816.54 seconds),
    including all 26 tolerance scenarios and five libsmi goldens. The workspace
    navigation/lifetime test passed AddressSanitizer with leak detection enabled.
    Release build/install and installed headless startup/restart passed, including
    profile-indicator navigation from Query and hiding Problems when leaving MIBs.
    Executable and all 355 installed MIB/PIB files match the build; ldd found no
    missing libraries. Diff whitespace and line-ending checks passed. No new product
    defects were found. The first combined validation session ended with SIGTERM
    before its final summary, without reported test failures; rerunning the complete
    suite in a dedicated execution session passed. This was an execution interruption,
    not evidence of a test flake. Existing Qt deprecation/unchecked setuid warnings
    and previously documented full-application leaks remain outside this layout step.
    Windows/macOS need CI. Five files changed without triggering scope guardrails;
    no desktop GUI or physical devices were used, and nothing was committed/pushed.
    Owner verified MIB-only Problems visibility after generating problems (2026-11-02).
    Build:
    `/home/synoptics/src/snmpb-next/build-linux-release`; install:
    `/tmp/mib-navigator-manual-validation/mib-problems`; executable:
    `/tmp/mib-navigator-manual-validation/mib-problems/bin/mib-navigator`.
18c. **Look.** Fusion style with Light, Dark and Follow-system themes. Remove
    hard-coded light-only colours (environment badge, request status,
    connection hint). One SVG line-icon set for actions instead of platform
    standard pixmaps. Remove titles that repeat their tab ("Current operation
    results", "MIB Object Inspector", "Raw SNMP and protocol output"). Show the
    environment state once instead of three times (header badge, permanent
    status-bar label, transient status message).
    Implemented (2026-11-02; validation completed 2026-11-03): one application-owned
    Appearance controller applies
    Fusion, persists only the theme preference in the existing settings root, and
    exposes View > Appearance > Follow System / Light / Dark. Follow System listens
    for platform colour-scheme/palette changes. Semantic item roles supply warning,
    failure, active-profile and paused styling through a palette-aware delegate;
    existing rows repaint without model resets. Semantic status labels paint from
    the current application palette, avoiding per-label stylesheet colour caches.
    Theme changes refresh resolved palettes on workbench controls and containers.
    Legacy Query HTML colours are adapted before insertion so streamed Walks
    do not trigger repeated document re-layout. Existing read-only text is
    recoloured in a batch on theme changes, preserving source query text and
    error classification. Editor syntax formatting changes
    colours without editing its source/undo state. Authored SVG action icons share
    one palette-aware renderer and Qt Svg is an explicit target dependency. The
    header indicator owns environment/startup presentation; duplicate permanent and
    transient environment status text and the Inspector's repeated heading retire.
    Validation: focused 9/9; complete deterministic suite 64/64, including all
    26 tolerance scenarios and five libsmi goldens; all 63 non-tolerance tests
    also passed after rebuilding every Debug target. The full-suite execution
    wrapper ended with signal 143 after CTest wrote all 64 passing results and its
    completed End testing record, before the wrapper status file was written;
    CTest result records were independently checked. Appearance-only ASAN with
    leak detection passed. Review caught and fixed stale Qt stylesheet palettes,
    theme initialization before the legacy settings root, and synchronous output
    recolouring that stalled large Walks. Query now maps HTML colours before
    insertion; theme switches batch document formatting. Final full-run Walk:
    3,000 objects in 3.29 s, longest UI stall 219 ms; Stop 184 ms.
    Release build/install, installed startup/restart, release-tree verification,
    byte comparison of all 355 bundled MIB/PIB files, dependency check (no missing
    libraries), and `git diff --check` passed. Existing Qt deprecations, unchecked
    setuid, obsolete UI tab-stop warnings and previously documented full-app
    legacy leaks remain outside this appearance step. Windows/macOS require CI;
    owner accepted the visible appearance/icons on 2026-11-03. Native Follow System
    remains dependent on desktop integration.
    No desktop GUI or real devices were used. Forty task files (including 17
    SVGs) changed; no scope guardrail triggered. Earlier uncommitted work was
    preserved; nothing was committed/pushed. Debug build:
    `/home/synoptics/src/snmpb-next/build-linux-debug`; Release build:
    `/home/synoptics/src/snmpb-next/build-linux-release`; install:
    `/tmp/mib-navigator-manual-validation/appearance`; executable:
    `/tmp/mib-navigator-manual-validation/appearance/bin/mib-navigator`.
18d. **Menus, Settings and Help.** Implemented, with owner corrections
    (2026-11-04): File, Edit, View, MIB, SNMP, Settings, Help. Settings contains
    Credentials, Preferences and Appearance. MIB contains Verify Current MIB
    (Editor-only, with visible location help) and Extract MIB from RFC (available
    everywhere; activates Editor). Stop remains on the GUI/context controls and
    Esc, with its redundant menu entry removed. Menu mnemonics are distinct;
    Settings retains Alt+T. Diagnostics exposes its application/SNMP log controls
    and data; About includes current capabilities, build UTC and source revision.
    Preferences has General, SNMP and Traps, preserving pending edits until OK.
    The redundant MIB Library preference page is retired. Collection configuration
    remains in MIBs > Library; global/fallback parser paths now come only from
    that collection. Active Profile plans still own loaded provider selection.
    Historical mibpaths/preload settings and files remain untouched, with no
    hidden provider precedence from that array. Query Macros is explicitly deferred
    near the top of todo.txt; its multiple-varbind replacement is not implemented.
    Validation: focused 5/5, complete deterministic 65/65 (27 scenarios/five goldens),
    focused Esc/large-Walk workflow, installed Release startup/restart and ASAN
    invalid-access checks passed. Build identity archive/reproducibility, release
    tree/MIB-byte/dependency and diff checks passed. Existing legacy leaks/warnings
    remain; Windows/macOS require CI. No desktop GUI, real device, commit or push.
    See [Settings/Help validation](settings-help-validation.md). Earlier checkpoints:
    [menus](menus-validation.md), [Preferences](preferences-validation.md).
    Verification/chooser follow-up (owner-authorized nonvisual fixes, 2026-11-04):
    Editor verification now uses a fresh serialized parser and a read-only copy
    of the full Library provider index, independent of the active Query profile.
    Private identity aliases support filenames differing from module names;
    unsaved text wins, real diagnostics remain, and latest-profile restoration
    preserves the published immutable Environment. OID selection uses a frozen
    model-based tree instead of eagerly allocating per-OID widget items/icons.
    It retains its captured interpretation across profile changes and recognizes
    scalar .0 instances. Focused 12/12, complete deterministic 65/65 (27 app
    scenarios/five goldens), ASAN focused 4/4 and app startup/restart, installed
    Release workflows, MIB-byte/dependency checks and diff/LF checks passed.
    Full-library chooser construction/selection: 57 ms for 25,704 nodes.
    Sixteen task files, no scope guardrail triggered; no desktop GUI, physical
    devices, commit or push. Windows/macOS require CI. See
    [verification/chooser validation](verification-chooser-validation.md).
    Editor responsiveness follow-up (2026-11-04): Verify now returns immediately
    and completes through a worker-owned snapshot, with busy controls and stale
    document/Library-context rejection. Metadata uses published values while
    refresh/profile requests defer; completion restores the latest Profile.
    Shutdown drains parser work safely and ignores late UI deliveries. Final
    focused 10/10, complete deterministic 65/65 (27 application scenarios and
    five goldens), ASAN 4/4, installed Release startup/restart, source/MIB byte,
    dependency and diff/LF checks passed. Nine task files; no scope guardrail,
    desktop GUI, physical devices, commit or push. Closing may wait for an
    uninterruptible parse; existing leak/warning and Windows/macOS CI limitations
    remain. See [asynchronous Verify validation](async-verify-validation.md).
18e. **Library and Profiles.** *Implemented in the accepted visual refresh
    (2026-11-04).* IANA catalog refresh and downloads live in **Get MIBs from IANA…**;
    opening uses cached data and does not start a network request. Downloads capture
    their destination. The obsolete Check for Updates placeholder is removed because
    the catalog has no reliable comparable revision data. Profiles has an **Edit
    profile** selector independent of Query's active selection. Folder-tree Add/Remove
    remains. See [visual refresh validation](visual-refresh-validation.md).
    Library dependency-check runtime consistency completed separately as a
    nonvisual fix (2026-11-03). Uncached checks use a fresh serialized parser
    with Library-only paths, then force restoration of the latest requested
    Profile after the Library UI refresh. Changed files are rechecked; roots
    come from the refreshed index so additions/removals participate immediately.
    Equivalent duplicate providers remain supported, failed index writes do
    not mark a check current, and a versioned check key prevents reuse of
    obsolete semantic results while retaining the provider graph. Focused 9/9,
    complete deterministic suite 64/64
    (27 application/tolerance scenarios, five libsmi goldens), installed Release
    workflow and ASAN workflow passed. The Library/Profiles UI changes above were subsequently
    completed in the visual refresh; legacy search-path retirement was completed in 18d. See
    [consistency validation](mib-environment-consistency-validation.md) for
    architecture, limitations, warnings and exact build/install paths.
    Responsiveness follow-up (2026-11-03): discovery, semantic checks and
    inventory preparation now use worker-owned snapshots; the UI publishes once
    without waiting for the parser or duplicating Library rescans. Profile changes
    and refreshes defer safely, and files changed during checking invalidate the
    report and request retry. Focused 9/9, nine dependency workflows (eight button actions and one deterministic
    worker-phase injection; 53 assertions each) in Debug/installed Release/ASAN, and complete deterministic
    64/64 (903.62 seconds; all 27 scenarios and five goldens) passed.
    Strict widget ASAN/leak checks, Release install/startup/restart, all 355
    installed MIB/PIB comparisons, unchanged repository MIB bytes, ldd and diff
    checks passed. Existing warnings/application leaks and Windows/macOS CI
    remain. No desktop GUI, physical devices, commits or pushes. Thirteen task
    files; no scope guardrail triggered. Latest install:
    `/tmp/mib-navigator-manual-validation/dependency-responsive`.
19. Optional: **implementation check**. Walk the live device and compare it
    with the selected profile (objects defined but missing, objects returned but
    undefined, values that break MIB rules).
20. Release builds for Windows, Linux, and macOS.

## Competitive feature review summary

| # | Feature | Decision |
|---|---|---|
| 1 | Compilation diagnostics | Step 13, merged with #6 |
| 2 | Raw vs interpreted | Step 9 |
| 3 | Table browser | Step 17 (largely built; verify and polish) |
| 4 | PDU tracing | Step 12 |
| 5 | Offline walks | Skipped |
| 6 | Symbol resolution | Step 13 |
| 7 | Indexed search | Step 14 |
| 8 | Graphing | Removed; Watch instead |
| 9 | Compiled representation | Step 15, only if needed |
| 10 | Notification inspection | Mostly built; step 11 |

## Known issues found during planning

- **MIB provider hashes depend on line endings.** `mibcollection.cpp` and
  `mibdependencyindex.cpp` hash raw file bytes with SHA-256. The same MIB with
  CRLF and with LF line endings therefore counts as different content. This
  affects duplicate detection, provider pins (path plus raw SHA-256), and the
  portability of saved profiles between Windows and Linux/macOS checkouts.
  Provider pins and MIB collections created on the old Windows setup may not
  match on the Linux VM. Step 2 must not change committed MIB bytes. A product
  fix, such as treating line-ending-only differences as identical, needs its
  own decision and tests.
- **Mixed line endings from tool edits.** In the old Windows checkout, 36
  edited files contained both CRLF and LF lines. Step 2 resolves this when
  carrying the work over.
- **First Linux build.** Development moved from Windows/MSVC to Linux/GCC.
  Some tests use Windows-style paths such as `C:/...` as fixture strings and
  may behave differently on Linux. Step 2 establishes the baseline.
- **User MIB collection and settings.** The Windows MIB collection
  (`Documents/MIB Navigator/MIBs`) and QSettings are not on the Linux VM.
  Manual validation needs the MIB collection copied to the Linux equivalent.
- **MIB tree Find stores a `QModelIndex`** (`mibview.h`, `find_last`) across
  operations. Review for model-reset safety when the tree or search is reworked
  (steps 7 and 14).
- **Graph series reference Agent Profile IDs.** This disappears with step 5,
  which comes before the address book is removed in step 8.
