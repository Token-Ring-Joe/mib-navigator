# Dialog consistency and smaller-window validation

Owner approved follow-up: 2026-11-04. Implementation and automated validation
are finished; ready for owner visual/interaction review.

## User-facing behavior

Editor Find, Replace and Go to Line now use consistent margins, aligned fields
and standard Qt button rows. Find and Replace remain open while their actions
run; empty search text disables search/replacement actions. Go to Line starts
at line 1 and uses a descriptive confirmation button. Native Qt button roles
control platform ordering rather than a hard-coded arrangement.

Preferences scrolls its section content while keeping OK/Cancel visible.
Credentials scrolls its editable fields while retaining the unified name/version
list and a fixed confirmation footer. Longer forms can wrap rows. Duplicate and
Delete do not become default buttons when navigating the dialog with Enter.

OID selection has consistent spacing, label buddies and a tree that takes the
available vertical space. The selected Environment and chooser-owned model
snapshot are unchanged. Instance selection has a descriptive heading, explicit
Cancel and a selection action disabled until an instance is selected. Manual
instance entry labels the numeric suffix and accepts compound suffixes such as
1.4. Diagnostics has a standard Close footer.

A genuine bug found during this pass is fixed: dismissing manual instance entry
previously performed the selected operation against .0. Cancel now issues no
Get/Get Next/Get Bulk request. Submission reads the current field directly;
there is no shared line-edit pointer or stale cached suffix.

## Architecture and review

The shared dialog helper owns presentation only: margins, spacing, initial
screen-aware sizing and action-button creation. Existing controllers still own
validation, working records, request context and persistence. No settings keys,
credential identities, MIB Environment ownership, collection indexing, parser
behavior or source MIB bytes changed. Scrolling containers do not create a
second representation of application state. The three main workspaces and
connection bar are unchanged.

Signals use dialog/button QObject contexts and are destroyed with the widgets.
Instance lists retain copied suffix strings. No model index or parser pointer
was introduced or retained across resets. Existing approved uncommitted work
is preserved; this pass makes no commit or push.

## Validation results

- Focused tests: 5/5 passed (29.14 seconds), including application startup and
  restart, credentials, appearance and Environment consumers/transitions.
- Actual modal Editor workflows verified Find by Enter, Replace/Replace All,
  Go to Line cursor movement, and standard Close behavior.
- Cancel with an entered suffix was checked for Get/Get Next/Get Bulk; the
  request log remains unchanged. Existing synthetic SNMP acceptance cases now
  submit their original requests through the real manual instance dialog and
  check the exact typed OID, without adding requests or contacting devices.
- Preferences and Credentials were checked at 640 x 480 for visible footers
  and access to their final SNMP/privacy fields. All three main workspaces fit
  at 1024 x 720; controls fit and navigation stays at y=100 in every workspace.
  Offscreen screenshots were inspected.
- Complete deterministic suite: 65/65 passed (1037.56 seconds), including all
  27 tolerance scenarios, synthetic SNMPv1/v2c query/table/Walk/Stop workflows,
  and close-during-startup safety checks.
- Five libsmi goldens: 5/5 passed in the complete run.
- ASAN changed workflows: 8/8 passed (60.60 seconds), including actual
  application startup/restart. Strict widget leak checks: 5/5 passed
  (3.58 seconds), with leak detection enabled. Broad application checks used
  detect_leaks=0 because of known legacy application/libsmi leaks.
- Release build/install and VerifyReleaseTree passed. Installed application
  startup and restart passed offscreen at normal and 150% scaling. ldd resolved
  all dependencies. All 355 packaged MIB/PIB files match their sources, and all
  401 source MIB/PIB files match the pre-task hashes.
- Final git diff --check passed. Frozen implementation hashes match, changed
  files use LF, all 401 source MIB/PIBs are unchanged, and the exact task scope
  is 17 expected files. No unexpected scope expansion occurred.

The first focused run passed functional checks but failed two compact-layout
assertions. Investigation found test setup issues: sizing the main window before
its newly selected workspace settled, and measuring a hidden Credentials
viewport. Settling the layout and showing Credentials offscreen before measuring
fixed the checks. The size limits were unchanged and the rerun passed.

The difficult-MIB run exposed a genuine additional layout defect: long
Environment warning text forced a minimum width of 1112 pixels. The badge now
wraps its complete text within the connection header. The syntax-error fixture
then passed at 1024 x 720 in Query, MIBs and Traps, with navigation at y=100 and
all connection controls visible. The initial failing run was stopped, the exact
warning fixture and focused tests were rerun, artifacts were rebuilt, and the
complete suite was restarted without relaxing assertions.

## Limits and owner review

This pass covers dialog consistency and a smaller-window review. A general
compact-density preference, comprehensive empty/busy-state wording, a whole-app
keyboard/focus audit and remembered-layout review remain separate refinements.
The temporary multiple-varbind workflow remains pending the deferred Query
Macros design. No new feature was added to it.

No desktop GUI was launched, real-device SNMP performed, or external IANA
request made. Synthetic SNMP acceptance uses the existing local scripted agent.
Windows and macOS need GitHub Actions checks; no push was made to trigger CI.
Known full-application/libsmi leak limitations require disabling leak detection
for broad ASAN application checks; strict widget checks use leak detection.
Compiler and offscreen warnings are recorded in the final result below.

## Stable manual-validation artifact

- Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`
- Release build: `/home/synoptics/src/snmpb-next/build-linux-release`
- ASAN build: `/home/synoptics/src/snmpb-next/build-linux-asan`
- Install: `/tmp/mib-navigator-manual-validation/dialog-polish`
- Executable: `/tmp/mib-navigator-manual-validation/dialog-polish/bin/mib-navigator`
- Install dependency check: passed, no missing libraries.

Minimum owner review: open Preferences and Credentials, resize the window, and
inspect the Editor's Find/Replace/Go to Line dialogs. Confirm the controls and
spacing feel consistent. Automation already validates their functional paths.

Existing compiler warnings remain visible: deprecated QMessageBox/QLibraryInfo
calls and an unchecked setuid result in the existing Linux privilege setup. Qt
offscreen emits its known propagateSizeHints warning. No compiler warning was
suppressed. The callback lifetime review bound enable-state callbacks to the
buttons they update, so teardown disconnects them before a later list/field
destruction can signal a destroyed button.

Final task scope: 17 files (15 source/header/UI/build files and two documents).
No file-scope guardrail was triggered or unexpected expansion introduced. No
generated deployment output was tracked.
