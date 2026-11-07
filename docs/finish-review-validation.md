# Finishing review and reboot recovery — 2026-11-07

Owner-requested order: remaining usability refinements; Query Macros design and
estimate; documentation reconciliation; a fresh validated manual install.
Earlier approved work is preserved. The validation pass made no commit or push;
the later owner approval is recorded below.

## Behavior and fixes

- View > Keyboard Navigation exposes the existing Tree, Work Area, Inspector,
  panel and collection-search actions, plus Ctrl+L for Target and Ctrl+Shift+L
  for Go to OID. Work Area reaches real controls in Results/Watch/Search,
  Library/Profiles/Editor and Traps. Explicit Inspector/panel focus reveals them.
- Focus routing resolves stable control names on invocation. It changes neither
  Qt's focus chain nor model state. Real Tab traversal reaches all enabled Query
  connection, search/filter, Results and Inspector controls. Search Enter opens
  a result through the same activation path as a double-click.
- Results distinguishes waiting for a first response from filter-hidden rows.
  Library explains filtered-empty results; empty Automatic Profiles direct users
  to their product folder. Empty Watch/Log/Traps explain the next useful action.
  Log Copy/Clear disable when empty; Search retains input guidance and explains
  index-building state without asking for an unnecessary repeated search.
- Window restoration repairs lower controls falling below the available screen.
  Query splitter restoration rejects damaged bytes and repairs stale vertical
  orientation. Existing Inspector/panel visibility, pane sizes and MIB-only panel
  selection remain under their established settings owners.
- Watch menus copy stable row ID/state before entering a modal event loop.
  Search menus copy source path/line before a background publication can rebuild
  their result table. Neither reads a stale row/pointer on return.
- Removed obsolete tab-order references to retired module widgets.

Physical collection, provider/dependency index, Library, Automatic/Custom Profiles,
active Tree Environment and serialized libsmi ownership are unchanged. Presentation
routing adds no persisted state, parser pointers or stored QModelIndex values.
All source MIB bytes match the task-start hash manifest.

The agreed Query Macros design is in `query-macros-v1-design.md`: four working
milestones, 6–10 focused development days, read-only one-shot scope. The owner
agreed it on 2026-11-07; implementation remains deferred. Current-facing README,
roadmap, TODO,
settings, package, release and device-acceptance guides now describe the redesign;
historical validation reports retain their original evidence. Documentation also
records the absent global SNMPv3 context preference rather than claiming it exists.

## Validation status

- Focused final Debug workflows: **7/7 passed**, 24.63 seconds. These include real
  application startup/restart, Search menu mutation and Enter activation, actual
  Query Tab traversal, shell/layout, Results, Traps, Library, Credentials and themes.
- ASAN changed workflows: **8/8 passed**, 45.78 seconds, including Environment
  transitions and actual application startup/restart. Broad runs use
  `detect_leaks=0:halt_on_error=1` for the documented legacy application/libsmi leaks.
- Strict ASAN widget leak checks: **5/5 passed**, 3.06 seconds, with leak detection
  enabled (shell, Results, Library, Credentials and Appearance).
- The scripted Watch popup/connection-capture scenario also passed ASAN, 92.97
  seconds. Both previously failing tolerance scenarios passed individually.
- Complete deterministic suite: **65/65 passed**, 931.82 seconds, including
  **all 27 tolerance/scripted SNMP scenarios**, **five libsmi goldens (5/5)**,
  and close-during-startup safety. No unexplained failure or timing flake remains.
- Release build and fresh install passed. `VerifyReleaseTree.cmake` passed;
  installed `ldd` reports **no missing libraries**. Installed executable bytes
  match the Release build; all **355 packaged MIB/PIB files** match build/source.
- Installed offscreen startup/restart passed at normal and 150% scaling
  (20.04 and 21.09 seconds). The installed scripted Watch/connection-capture
  workflow also passed (81.20 seconds). All requests use the loopback test agent.
- Final source freeze matches all 1,943 implementation files.
  **`git diff --check` passed**; final static audit also checks scope, line endings,
  source MIB bytes, current Markdown links and preservation of baseline files.

The first focused run exposed two test setup errors: Qt reports an editable
Target's focus on its QComboBox, and an unscoped LogClear lookup found the separate
Diagnostics button. Expectations now follow Qt's focus owner and the request log
uses an unambiguous RequestLogClear name. A subsequent actual Tab test exposed an
intermediate implementation defect: focus proxies skipped preceding Results filter
controls. Stable-name routing replaced the proxies; unchanged Tab assertions pass.
No timing threshold or functional assertion was weakened.

The first full run passed 64/65 CTest entries but exposed two interactions inside
its tolerance runner: the new Search menu/Enter checks replaced the Editor document
needed by a later edit/save scenario, and the expanded Watch check dropped a log
line explicitly required by the runner. The Search regression now restores the
original source, filename, cursor/selection and modified state; Watch retains the
original removal assertion as well as the new modal-menu assertion. Modal test
timers are scoped to local QObject contexts. These are test-harness defects;
product behavior and scenario expectations were not relaxed. Both affected scenarios
passed individually before the final full suite. An initial all-target ASAN build
was deliberately stopped and resumed with the affected application/widget targets;
this was build scoping, not a test failure or timing flake. A later full-suite
run was deliberately stopped to correct empty-history wording: clearing Traps
or Log does not mean nothing arrived during the session. Clear now also has an
explicit empty-Traps-hint regression. The final pipeline starts again from builds
and focused checks; no partial run counts as a complete-suite result.

## Warnings and environment

The final Release build recorded 27 warning occurrences: existing deprecated
QMessageBox/QLibraryInfo/QDateTime APIs, an unchecked Linux `setuid` return, and
unchecked `QFile::open` results in existing Library tests. Debug/ASAN builds also
retain their warnings. None was suppressed. Exact messages are preserved in
`compiler-warnings.json` beside the build logs. Qt offscreen emits its known
platform/size-hint messages; no desktop window is launched. No unexplained
failure, timing flake or timeout remains after the corrected runs.

## Scope and limits

This finishing pass changes **26 expected files**: 15 application/UI/test files
and 11 documentation files, including the local engineering instructions. Earlier
approved uncommitted milestones and personal workspace files remain preserved.
No file-scope guardrail or unexpected expansion occurred. All edited files use LF;
all **401 source MIB/PIB files** retain their task-start bytes. Implementation
hashes cover **1,943 files** to ensure that final validation matches the source.
No generated repository `manual-validation/` output was edited or tracked.

Query Macros implementation, Watch-all/table expansion, a general density setting
and the global SNMPv3 context-name preference remain deferred. Existing broad
application/libsmi leak limitations remain; passing the broad ASAN workflows
does not establish a leak-free application. Windows/macOS await platform CI.

## Artifacts and owner review

Builds are on the active VM-local clone:

- Debug: `/home/synoptics/src/snmpb-next/build-linux-debug`
- ASAN: `/home/synoptics/src/snmpb-next/build-linux-asan`
- Release: `/home/synoptics/src/snmpb-next/build-linux-release`
- Persistent install: `/home/synoptics/mib-navigator-validation/finish-review-2026-11-07`
- Executable: `/home/synoptics/mib-navigator-validation/finish-review-2026-11-07/bin/mib-navigator`

The old temporary installs vanished after reboot. This successful replacement is
outside the repository and survives reboot. Evidence, per-stage exit statuses,
detailed full-suite output, compiler warnings, source manifests and the final
static audit are under:

`/home/synoptics/mib-navigator-validation/finish-review-2026-11-07-evidence`

The owner **approved the accumulated work and authorized a local commit on
2026-11-07**. Approval closes this finishing checkpoint; it does not assert a new
desktop or real-device test run. The validation above predates that commit; no
source implementation changed afterward. The architecture review found no new
competing MIB state authority. See [outstanding tasks](outstanding-tasks.md) for
remaining release work, planned features and separate maintenance decisions.

Optional repeat UX check: start the executable above, open Preferences and Credentials,
resize them, inspect Editor Find/Replace/Go to Line, and try View > Keyboard
Navigation and a restart after resizing the main panes. Confirm the interactions
feel consistent. No desktop GUI or physical SNMP device is used by this task;
Windows/macOS require CI after the owner authorizes the next push.
