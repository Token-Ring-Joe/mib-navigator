# Verify MIB crash fix — 2026-11-04

## Cause and behavior

The user's 12:27 crash dump points to QTreeWidget::clear called by
MibViewLoader::SetEnvironment during asynchronous profile restoration.
MibSelection owns a temporary BasicMibView. Closing its dialog deleted the tree,
but MibViewLoader retained an unguarded pointer. Verify restored the active
profile and wrote through that pointer. Other profile publications could trigger
the same defect after using the OID chooser.

A headless regression reproduced a heap-use-after-free in BasicMibView::SetDirty
under AddressSanitizer before the fix. The loader now stores QPointer references,
prunes deleted views, and uses a guarded snapshot while notifying live views.
This also handles one chooser closing another during selection reset and avoids
duplicate registration. Dialogs retain ownership of their trees. Immutable MIB
environments, active-profile authority, parser behavior and user settings are
unchanged.

## Regression workflows

- Populate and close a temporary OID tree, then publish another environment.
- Keep a chooser open across publication, clear its selection safely, and
  repopulate it from the current environment.
- Close a second chooser synchronously during another tree's selection reset.
- Open and cancel the real OID chooser, verify an opened local MIB through the
  Editor toolbar, and wait for successful asynchronous profile restoration.
- Retain the existing complete-library and repeated profile-transition tests.
  The lifetime fixture uses normal MIB-II applicability even in the stress run.

The consumer test previously omitted application icon resources. Instantiating
actual chooser widgets exposed repeated failed image-format probes. Its target
now includes snmpb.qrc with AUTORCC. Before that correction one slow test was
intentionally stopped to obtain a diagnostic core; that SIGABRT was diagnostic,
not a second application crash. An attempted debugger attachment was denied by
the OS ptrace policy; the core supplied the required stack instead.

An unbounded-library chooser also eagerly constructs all tree items, so its
population is substantially slower than the model-based main tree. This
pre-existing performance limit is outside the lifetime fix; no functionality is
removed. The scoped regression uses the ordinary MIB-II chooser workflow.

## Validation

- Focused Debug workflows: 4/4 passed (49.42 seconds).
- AddressSanitizer lifetime regression passed; standard-timeout application
  startup/restart smoke passed. detect_leaks=0:halt_on_error=1 was used because
  inherited libsmi/application leaks are already known; this is an invalid-access
  check, not a leak-free claim.
- Two early sanitizer smoke attempts hit the 45-second launch/reload limit while
  builds and the expensive full-tree fixture were competing for CPU. A diagnostic
  run with a temporary 180-second limit completed; the final run passed with the
  unchanged standard limit after those workloads finished. No production or
  committed test timeout was increased.
- Release build/install, installed startup/restart smoke, release-tree check and
  ldd passed. No missing shared libraries.
- 355 installed MIB/PIB files match source bytes; 401 repository MIB-tree files
  match the earlier baseline. Source freeze and LF checks passed.
- Existing warnings remain: deprecated Qt QMessageBox/QLibraryInfo calls,
  unchecked setuid result, obsolete UI tab-stop entries, and expected offscreen
  plugin messages. No warnings were suppressed.
- Complete deterministic suite: 65/65 passed (962.34 seconds), including
  all 27 application scenarios and all five libsmi goldens.
- Final git diff --check passed. Implementation is ready for manual UX validation.

Build directories:

- Debug: /home/synoptics/src/snmpb-next/build-linux-debug
- AddressSanitizer: /home/synoptics/src/snmpb-next/build-linux-asan
- Release: /home/synoptics/src/snmpb-next/build-linux-release
- Install: /tmp/mib-navigator-manual-validation/verify-crash
- Executable: /tmp/mib-navigator-manual-validation/verify-crash/bin/mib-navigator

Validation logs use /tmp/verify-crash-*; the full suite log is
/tmp/verify-crash-full-suite.log and the frozen source manifest is
/tmp/verify-crash-source-freeze.json.

Six files changed for this task: app/mibview.cpp, app/mibview.h,
app/mibenvironmentconsumer_test.cpp, app/main.cpp, app/CMakeLists.txt and this
report. The accumulated approved workstream has 53 paths; two personal untracked
paths remain untouched. No task scope guardrail triggered and there was no
unexpected expansion. No commit, push or desktop GUI launch was performed.
Windows/macOS still require GitHub Actions verification; nothing was pushed to
trigger CI.

Manual validation is limited to opening the same MIB in the executable above,
clicking Verify, and confirming diagnostics appear while the application stays
open. The closed-chooser restoration sequence is covered programmatically.
