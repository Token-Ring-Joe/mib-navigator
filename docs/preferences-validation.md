# Preferences validation — 2026-11-04

## Behavior and architecture

Preferences now has General, SNMP, Traps and MIB Library sections. Transport and
request defaults share SNMP; Traps groups listen ports, detail expansion and
retention. MIB Library shows the current collection folder read-only and retains
additional search folders until their separate ownership consolidation. General
contains hostname display and points to View > Appearance. The unused automatic
MIB-loading controls and obsolete split-layout controls are removed. Their saved
values and historical settings identity remain compatible.

Controls own a pending edit. A single applied settings snapshot supplies running
getters. Opening reloads applied values; Cancel/window close restores them. Section
changes retain pending edits. Reset only replaces the displayed folder list; it
never writes settings or starts a scan. OK saves and syncs before changing the
applied snapshot. Save failures are reported and do not emit live updates or a
restart notice. Runtime transport fallback remains a separate supported setter.
Qt owns the dialog widgets; the parented Preferences controller owns its UI wrapper.

Defects fixed: Cancel previously leaked checkbox/port edits into live getters;
Reset wrote folders and rescanned before acceptance; revisiting Modules discarded
pending folder edits; repeated Save appended old and current folders together;
and every Save refreshed/rebuilt the MIB environment even for unrelated settings.
Only accepted path changes now request that refresh. Existing GETBULK bounds,
preload ownership and MIB source bytes are preserved.

Validation also exposed a Problems-pane defect: selecting an already-current row
could leave its explanation blank because detail updates watched current-cell
changes. Details now follow row selection; clear and reselect are deterministic
regressions. No QModelIndex survives a model rebuild.

## Validation

- Focused and related tests: **4/4 passed**.
- Complete deterministic suite: **65/65 passed** (952.34 seconds), including
  27 application/tolerance scenarios and **five libsmi goldens**.
- Actual-dialog tests cover four sections, pending edits across navigation,
  Reset, Cancel, reopen, transport/path validation, OK/live trap retention and
  repeated-save path/legacy-settings preservation. Problems tests fix the current
  cell while changing selection, then clear and reselect the same row.
- Debug/Release builds and installed Release startup/restart passed.
- Full-app AddressSanitizer startup/restart passed with invalid-access checking;
  `detect_leaks=0` retains the documented legacy application leak limitation.
- Release-tree verification, executable byte comparison, **355 bundled MIB/PIB
  comparisons**, unchanged **401 repository MIB/PIB tree files**, and `ldd`
  (**no missing libraries**) passed. Source hashes stayed fixed during final checks.
- `git diff --check` and LF checks passed.
- The initial full run exposed the genuine Problems selection defect and was
  stopped. It was fixed and deterministically covered, then focused checks,
  installed/ASAN workflows and the complete suite were rerun. A subsequent full
  run exposed a test-fixture baseline captured before an
  asynchronous editor restore. The baseline now starts at dialog entry, with
  separate environment and persisted/live-state assertions; a focused previously
  failing scenario and the full suite were rerun. No final failure, flake or timeout.
  Existing Qt deprecations, unchecked `setuid`, obsolete main-UI
  tab-stop warnings and offscreen-platform messages remain; no new warning category.
- Seven milestone files; 36 workstream files remain uncommitted including prior
  fixes. Personal untracked files remain untouched. No scope guardrail triggered,
  no unexpected expansion, and no generated deployment output was tracked.

## Limits and manual validation

Legacy additional MIB paths still feed libsmi alongside the Library; retiring that
second source is a separate traced change in the authoritative plan. Network
transport and trap listen-port changes still require restart. QSettings uses its
existing atomic INI synchronization; a failed write is reported without applying
running changes. No settings-root migration is introduced. Previously documented
full-application legacy leaks and native Follow System limitations remain.

Ready for desktop UX validation: open Tools > Preferences; review the four sections.
Change a value and click Cancel, then reopen to confirm it stayed unchanged. Try
Reset to defaults under MIB Library, then Cancel. Finally change a harmless request
timeout, click OK and reopen to confirm it saved. Choose your preferred value afterward.
No desktop GUI or real SNMP device was used by the agent. Windows/macOS need CI.
No commit or push was made.

Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`.
Release build: `/home/synoptics/src/snmpb-next/build-linux-release`.
ASAN build: `/home/synoptics/src/snmpb-next/build-linux-asan`.
Install: `/tmp/mib-navigator-manual-validation/preferences`.
Executable: `/tmp/mib-navigator-manual-validation/preferences/bin/mib-navigator`.
