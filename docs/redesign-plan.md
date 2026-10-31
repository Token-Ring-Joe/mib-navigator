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
- **MIB Profile**: dropdown of Automatic, Custom, and built-in `All MIBs`
  profiles.
- **Detect** button next to the MIB Profile dropdown (see Phase 3).
- Port, timeout, retries, GETBULK max-repetitions, and transport are **global
  settings** reached from the menu bar. They are not shown in the main window.
  SNMPv3 context name is also global for now. Per-query context may be added
  later if VRF contexts are needed.

### Traps

- Decode with the **currently selected MIB Profile**. The user selects
  `All MIBs` for multi-vendor decoding.
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
   user decides; the user commits.
4. **Mark the baseline.** The user commits or tags a known-good copy (for
   example `before-redesign`).

### Phase 1: Removals

5. Remove Graphs and the vendored Qwt target.
6. Remove Discovery.

### Phase 2: Layout and connection model

7. Main window layout per the target layout, building on the workbench shell.
8. Replace the address book with the connection bar in a **single working
   step**: Target history, Credential dropdown, MIB Profile dropdown, the
   Credentials screen, and global connection settings. Graph-era and
   Discovery-era dependencies on Agent Profile IDs must be gone by the end of
   this step.

### Phase 3: Features, in dependency order

9. **Raw vs interpreted values with mismatch warnings.** Show the received
   ASN.1 type and raw value beside the MIB interpretation (named values,
   DISPLAY-HINT, units). Warn on wrong type, undefined enumeration value,
   out-of-range value, and size violations. Used by Results, Watch, Traps, and
   Tables.
10. **Watch.**
11. **Traps**: Copy source IP, plus raw/interpreted varbinds from step 9.
12. **Log / PDU view**, starting with plain-English SNMPv3 REPORT translation
    (unknown user, wrong auth, decryption failure, time window, unknown engine
    ID). Full packet decode is optional later.
13. **MIB Problems view** (competitive features #1 and #6 merged). Actionable
    messages for missing imports, unresolved symbols, duplicate and conflicting
    providers, and parse diagnostics. Show where each definition came from and
    why that provider was chosen.
14. **Collection-wide search** (#7) by name, qualified name, or numeric OID
    across the whole MIB collection, not only the loaded tree. Results say which
    module and which profiles contain the match.
15. **Persist compiled MIB data** (#9), only if measured search or startup
    performance requires it.
16. **Detect profile.** Reuses the collection-wide index from step 14.
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
