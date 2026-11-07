# Menu organization validation — 2026-11-04

## Behavior and scope

The main menu bar follows File, Edit, View, MIB, SNMP, Tools, Help.
Edit holds Find, Find Next, Replace and Go To Line; MIB holds Verify MIB syntax;
SNMP holds Credentials, Multiple Varbinds and Stop Walk; Tools holds Extract
MIB from RFC and Preferences. File commands, View navigation/panel commands,
Help Diagnostics, action enablement and keyboard shortcuts retain their existing
connections. Preferences dialog redesign is the next part of step 18d.

The UI declaration owns menu order. WorkbenchShell populates that declared View
menu, rather than inserting a second menu at a hard-coded position. Its fallback
for standalone shell construction remains. No settings, parser, credential,
transport, MIB Library/Profile ownership or asynchronous lifetime change.

Review found the shell's previous insertion position would put View before Edit;
using the declared menu fixes that conflict. Tests exercise the actual main UI's
menu order, command placement and shortcuts, and the shell's navigation/panel
behavior with the declared seven-menu bar. No additional product defect was
found during validation. Existing accepted MIB/Traps changes are preserved.

## Validation

- Focused workbench, Preferences settings, appearance and startup/restart: **4/4 passed**.
- Complete deterministic suite: **65/65 passed** (1195.87 seconds), including
  all 27 application/tolerance scenarios and **five libsmi goldens**.
- Focused workbench AddressSanitizer test: **1/1 passed**, leak detection enabled.
- Debug and Release builds passed. Installed Release startup/restart passed,
  including actual menu order, command placement and shortcut assertions.
- Release-tree verification passed; executable bytes match the Release build;
  all **355 installed MIB/PIB files** match their sources. All **401 repository
  MIB/PIB tree files** retain their baseline bytes.
- Installed `ldd` dependency check passed: **no missing libraries**.
- `git diff --check` and LF checks passed.
- Existing Qt deprecations, unchecked `setuid`, obsolete UI tab-stop warnings,
  and offscreen platform capability messages remain. No new warning category,
  test failure, flake or timeout was found. No full-application leak-free claim
  is made; the previously documented legacy leak limitations still apply.
- Six files changed in this milestone (four source/test files and two documents).
  Thirty-four workstream files remain uncommitted including the preceding MIB
  and Traps milestones; two personal untracked paths remain untouched. No scope
  guardrail triggered and no unexpected file expansion occurred.


## Manual validation and remaining work

Ready for a desktop menu check: launch the executable below, confirm the seven
menu titles, open SNMP > Credentials and Tools > Preferences, then close them
with Cancel. Check that View still opens Library and Results. The Preferences
layout and known Cancel/Reset behavior are separate pending work; this milestone
does not change them. Native macOS menu relocation and Windows behavior require
CI/platform validation. No desktop GUI or physical device was used by the agent.
No commit or push was made.

Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`.
Release build: `/home/synoptics/src/snmpb-next/build-linux-release`.
ASAN build: `/home/synoptics/src/snmpb-next/build-linux-asan` (workbench target).
Install: `/tmp/mib-navigator-manual-validation/menus`.
Executable: `/tmp/mib-navigator-manual-validation/menus/bin/mib-navigator`.
