# Project review and resume checkpoint — 2026-11-08

## Current workstream

The working clone is `/home/synoptics/src/snmpb-next`, branch
`modernize/qt6-cmake`, HEAD `02a5f45`. The NAS working tree remains archival.
The initial worktree had 92 modified/untracked file entries from approved
follow-up, maintenance and Query Macros milestone 1 work, plus personal files.
All existing changes are preserved. No commit, push or publication was performed.

Query Macros milestone 1 implements offline definitions, atomic storage and the
Query-local editor. Creation, editing, selection and restart send no requests.
It remains ready for owner desktop UX review. This review corrected the agreed
design's stale “in progress” status; no product defect requiring a code change
was identified in the reviewed paths.

## Architecture and next step

The non-widget macro service owns working/committed definitions; its repository
owns versioned storage, write locking and external-change detection. Widgets
submit copied values. Numeric OIDs and stable IDs survive profile/model resets;
object selection uses immutable Environment metadata and a frozen tree model.
Macros do not persist endpoints, credentials, Profiles or parser/model pointers.
Library/provider/Profile/Environment ownership remains unchanged. The legacy
Multiple Varbinds dialog remains deliberately outside macro definition ownership
until the agreed replacement milestone.

The next planned step is milestone 2: captured scalar/exact-instance runs,
bounded GET batches, grouped Results, Stop and request logging. Its authorization
question is pending. The explicit milestone boundary in
[the redesign plan](redesign-plan.md#query-macros-priority-and-milestone-boundary)
continues to apply; this review does not authorize later implementation.
Integrate orchestration through existing foreground Query ownership rather than
repeatedly invoking `Agent::RunSingleRequest`, which resets Results and request
state. Table retrieval belongs to milestone 3; legacy dialog retirement to 4.
Watch-column/table expansion remains separately deferred.

## Fresh verification

- Debug and Release incremental builds passed using GCC 15.3.0 and Qt 6.11.2.
- Focused Debug macro/editor/workbench tests passed 3/3; the application offscreen
  launch/save/restart smoke passed separately (1/1).
- Complete deterministic Debug suite passed **69/69** in 1,055.05 seconds,
  including all **27 tolerance scenarios** and **five libsmi goldens**.
- All five unchanged libsmi diagnostic goldens passed in Debug and Release.
- Focused Release checks passed 11/11, including macros, workbench, startup,
  build identity, release policy and all five goldens.
- Strict AddressSanitizer macro/editor/workbench checks passed 3/3 with leak
  detection enabled; no suppressions.
- Fresh persistent Release install passed release-tree verification, executable
  byte comparison, all 355 bundled MIB/PIB source-byte comparisons, and `ldd`
  with no missing libraries. Installed offscreen startup/restart, macro storage
  and safe disabled-transport failure passed.
- Source-preservation hashes cover 2,042 existing files; only the intentional
  design-status correction differs. No source MIB bytes changed.
- `git diff --check`, documentation LF/trailing-whitespace checks and the final
  preservation/scope audit passed. No unexplained failure, flake or timeout remained.

No desktop GUI or physical-device SNMP was used. Existing parser leaks and
vendor compiler warnings remain as documented in
[maintenance validation](maintenance-five-validation.md); the incremental builds
emitted no new compiler warnings. Qt offscreen capability messages are expected.
Windows/macOS native CI, desktop acceptance and separately authorized lab tests
remain outstanding. External-change recovery still requires macro Reload;
there is no merge/save-as recovery workflow.

## Validated artifact and minimum owner check

- Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`
- Release build: `/home/synoptics/src/snmpb-next/build-linux-query-macros-m1-release`
- Install: `/home/synoptics/mib-navigator-validation/resume-2026-11-08`
- Executable: `/home/synoptics/mib-navigator-validation/resume-2026-11-08/bin/mib-navigator`
- Dependency check: passed; no missing libraries.

Open this executable, create a macro in Query > Macros, add an object, Save,
then close and reopen it. Confirm the definition is retained and the editor
feels clear. Macro execution is not part of milestone 1.

Scope: two documentation files changed by this review (this record and the
agreed-design status). No application/build-policy files changed. No file-scope
safety guardrail or unexpected scope expansion was triggered.
