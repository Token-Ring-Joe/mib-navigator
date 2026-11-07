# Settings, Help and MIB-path cleanup — 2026-11-04

## Implemented behavior

The menu bar is File, Edit, View, MIB, SNMP, Settings, Help. Settings owns
Credentials, Preferences and Appearance. MIB owns Verify Current MIB and Extract
MIB from RFC. Extraction is available from every workspace and activates Editor
before choosing a file. Verify remains enabled only in MIBs > Editor; its tooltip
explains that location. The redundant Stop menu entry is removed; the window owns
the same QAction so Esc still cancels operations, and existing visible/context
Stop controls remain.

Diagnostics now explicitly shows the former tab page after reparenting it into
its separate window. Opening it records a diagnostic entry and exposes accumulated
application/SNMP messages and controls. Turning SNMP library logging off keeps
existing application diagnostics readable. About describes the engineering tool's
current capabilities and includes build UTC, source revision and local-change
state, alongside existing version, dependency and license/attribution information.
A small build-time CMake target refreshes a separate generated identity header;
the timestamp stays out of the widely included version header. Source archives
without Git remain supported; SOURCE_DATE_EPOCH supports reproducible timestamps.
The release label is unchanged; no release/tag is implied.

Preferences now contains General, SNMP and Traps. MIB collection location remains
managed in MIBs > Library. Its redundant Preferences page and path-editing/reset
controls are removed. Cancel/OK behavior and live trap-limit application remain.
The controller no longer initializes or saves the historical mibpaths array.
The settings serializer has an explicit preservation mode for both legacy paths
and preloads; historical keys and source folders remain untouched.

## Architecture and review

Global/fallback parser search paths now come only from the visible MIB collection.
ReadMibPaths previously prefixed the collection with the separate persisted
mibpaths array, creating conflicting provider precedence. That second authority
is retired. The provider index, Library inventory, Automatic profiles and Custom
profile selection continue their existing responsibilities. Active profile
runtime plans still own actual loaded provider selection and tree construction;
Library semantic checks and metadata already use Library-owned paths. No parser
behavior, source-MIB bytes, profile schema, credentials or settings identity changes.
No QModelIndex or parser-owned pointer is retained across a reset by these changes.

Removing a generated UI dependency exposed Agent's missing direct QListWidget
include, which is now explicit. Agent behavior is otherwise unchanged. The startup
harness's old assertion requiring installation paths in Preferences was obsolete;
it now verifies that a real retained legacy-only provider folder is absent from
the startup parser configuration while Standards/Unassigned are present. Existing
legacy arrays survive actual dialog saves and restart. This is intentional
retirement, not a destructive migration: external old folders are left on disk,
and MIBs can be imported into the Library when needed.

## Validation

- Focused product identity, workbench, Preferences settings, appearance and
  actual application startup/restart checks: **5/5 passed**.
- Complete deterministic suite: **65/65 passed** (895.98 seconds), including
  all **27 application/tolerance scenarios** and **five libsmi goldens**.
- Focused local-agent large Walk/Stop scenario passed, including a real Esc key
  event triggering the Stop QAction without a menu entry. The full suite repeats it.
- Actual window checks verify visible populated Diagnostics, readability with
  library logging disabled, About build identity, menu placement/unique mnemonics,
  extraction file selection from all three workspaces, Editor-only verification,
  three Preferences sections, Cancel/OK and untouched historical path settings.
- Debug/Release builds passed. Installed startup/restart and full-app ASAN
  invalid-access startup/restart passed. `detect_leaks=0` retains the documented
  legacy application leak limitation; this is not a leak-free application claim.
- Build identity without Git and reproducible SOURCE_DATE_EPOCH checks passed.
- Release-tree verification, installed executable byte comparison, **355 bundled
  MIB/PIB comparisons**, and unchanged **401 repository MIB/PIB tree files** passed.
  Installed `ldd`: **no missing libraries**. Source hashes remained fixed.
- `git diff --check` and LF checks passed.
- Validation corrected an obsolete harness assertion requiring writes to retired
  paths, and a Problems selection assertion that yielded to an unrelated editor
  report restore before checking a synchronous update. No product behavior was
  changed to satisfy those tests. Review enabled actual MIB menu tooltips and
  removed the SNMP/Settings mnemonic collision (Settings retains Alt+T).
- No final test failure, flake or timeout. Existing Qt API deprecations, unchecked
  `setuid`, obsolete main-UI tab stops and offscreen platform messages remain.
- **25 task files**, **50 workstream files total** including earlier accepted
  uncommitted work. Two personal untracked paths are untouched. No scope guardrail
  triggered, no unexpected expansion, and no generated deployment output tracked.


## Deferred scope and manual review

Query Macros is the first/high-priority deferred item in todo.txt and is recorded
in the authoritative redesign plan. Named persistent read collections, grouped
results and scalar/table instance rules remain design work; no macro implementation
or multi-varbind workflow redesign was made. The existing multiple-varbind New/Edit
SET-only limitation therefore remains pending that replacement. Single-object SET
is unchanged. IANA/Profiles cleanup remains the next separate planned step.

Ready for desktop review: open Help > Diagnostics and About; confirm visible log
controls/data and build information. Review Settings > Credentials/Preferences/
Appearance; Preferences has three sections. From Query or Traps, choose MIB >
Extract MIB from RFC and Cancel the file chooser; Editor should be selected and
Verify Current MIB enabled there. GUI appearance/native menu placement remains
owner validation. No desktop GUI or physical SNMP device was used by the agent.
Windows/macOS require CI. No commit, push or tag was made.

Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`.
Release build: `/home/synoptics/src/snmpb-next/build-linux-release`.
ASAN build: `/home/synoptics/src/snmpb-next/build-linux-asan`.
Install: `/tmp/mib-navigator-manual-validation/settings-help`.
Executable: `/tmp/mib-navigator-manual-validation/settings-help/bin/mib-navigator`.
