# MIB Navigator Redesign Plan

Status: **Accepted direction (2026-10-30)**. This document is the authority for
product direction and build order. Where older design documents in `docs/`
disagree with it, this document wins. Standing engineering rules remain in
`AGENTS.md`.

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
  SNMPv3 user). Managed on a separate Credentials screen. Continue the existing
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
  SNMPv3 context name is also global for now. Per-query context may be added
  later if VRF contexts are needed.

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
+----------------------------------------------------------------------+
| Target [10.1.1.20 v] Credential [Lab v] MIB Profile [Fabric Engine v] [Detect] |
| [Get] [Next] [Walk] [Bulk] [Set] [Table]           Search all MIBs... |
+---------------+------------------------------------------------------+
| MIB tree      | Results (raw | interpreted | warnings)               |
|               +------------------------------------------------------+
|               | Object details (syntax, range, description, source)  |
+---------------+------------------------------------------------------+
| [Watch] [Traps] [Log / PDU] [MIB Problems]          (bottom tabs)    |
+----------------------------------------------------------------------+
```

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
   that answers one object with the wrong type. Still to do: show it in the Query
   Table view, Traps, and Watch. Not checked yet: DISPLAY-HINT mismatches and
   values that violate a TEXTUAL-CONVENTION beyond range, size, and enumeration.
10. **Watch.** *Done (2026-10-31):* right-click a row in Results and choose "Watch this value".
    The Watch tab (bottom panel) lists each watch with its own target and credential, value,
    change since the previous poll, rate per second, the raw received type, any MIB warnings,
    and its status. Rates use the device's own sysUpTime when it answers (so polling jitter does
    not distort them) and fall back to the local clock; "octets" objects show bits per second.
    A Counter32 wrap is told from a device restart by sysUpTime (no sysUpTime: a fall from the
    top quarter of the range is a wrap, anything else a reset), so neither shows a false spike
    and a restart resets the baseline. A row captures the target text and a credential
    reference; the credential is re-resolved at every poll, and the connection bar never
    retargets a row (`snmpb.watch-service`, `snmpb.watch-calculator`, and the loopback
    scenario, which points the bar elsewhere while a watch keeps polling). Not done yet:
    watches are not saved across restarts, there is no per-row interval edit, and Watch cannot
    be started from the Tree.
11. **Traps**: Copy source IP, plus raw/interpreted varbinds from step 9.
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
    keeps the newest 1000 entries. Not done: the table and instance-picker requests
    are not logged yet, Watch polls are not logged, and there is no raw packet
    decode.
13. **MIB Problems view** (competitive features #1 and #6 merged). Actionable
    messages for missing imports, unresolved symbols, duplicate and conflicting
    providers, and parse diagnostics. Show where each definition came from and
    why that provider was chosen. *Foundation done (2026-10-30): tolerant
    loading, a structured problem report with parser line numbers, and a
    Problems tab (see `mib-architecture.md`). Remaining: filtering and sorting,
    "why this provider" detail, and copy/export.*
14. **Collection-wide search** (#7) by name, qualified name, or numeric OID
    across the whole MIB collection, not only the loaded tree. Results say which
    module and which profiles contain the match. *Done (2026-10-31):* the "Search all
    MIBs..." box in the query toolbar fills a Search tab in the bottom panel. It searches an
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
    is built in. **Limit:** Automatic Profiles generated from `Vendor/Product` folders do not
    exist in the code yet (folder snapshots become Custom profiles), so Detect considers every
    saved profile except the built-in Standards profile, which would match every device.
17. **Table verification** (#3) against real vendor tables (sparse rows,
    string/IP indexes, IMPLIED, AUGMENTS), plus Copy as CSV.

### Phase 4: Finish

18. Visual polish: icons, consistent wording, optional dark mode, keyboard
    shortcuts, remembered window layout.
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
