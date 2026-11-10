# Five autonomous follow-up items — 2026-11-07

Owner request: complete five items without further input. Work starts from
approved commit `02a5f45`; personal untracked files remain untouched. No commit,
push, desktop GUI or real-device SNMP is part of this follow-up.

## Implemented behavior

1. **Global SNMPv3 context:** Settings > Preferences > SNMP provides an exact
   context name; empty keeps the default. Save/Cancel and restart preserve the
   established settings root. New requests capture the UTF-8 bytes, while existing
   operations retain their value. SNMPv1/v2c do not acquire a context.
2. **Saved Watch definitions:** automatic atomic persistence of endpoint,
   credential reference, numeric instance, context and interval. Restart restores
   every row paused with a fresh baseline, so startup sends no polling requests.
   Secrets, previous results and Profile/parser objects are never saved.
3. **Selected-row Watch intervals:** Interval…, double-click Every, or the
   context menu changes only that row (1–3600 seconds). Cancel preserves it;
   changing an interval neither resumes nor retargets a paused watch.
4. **MIB Problems tools:** text/severity filtering, semantic severity sorting,
   CSV clipboard copy and atomic file export. CSV follows the visible sort/filter
   and includes complete paths, lines, causes, effects, requesters and suggestions.
   Opening the chooser captures the export before later filter/Profile updates.
5. **Consistent diagnostics:** raw SNMP++ and parser messages use the existing
   structured file/UI logger, with milliseconds/UTC offset on every physical line.
   Library text remains intact inside the wrapper, except redacted secret values.
   Delivery captures its recipient/session and cannot drift to another widget.

## Ownership and adversarial review

WatchService remains the single non-widget definition/runtime owner; widgets
project its rows by copied IDs. Credentials resolve by saved reference at each
poll. UI-created watches freeze the resolved port and context before the instance
picker; later connection/global-context changes cannot redirect them. Interpretation
still comes from the active published Environment per poll. No second MIB store,
provider selection, parser behavior or legacy preload authority was introduced.

The Watch file is private AppLocalData, with isolated test roots. Loading validates
the entire bounded/versioned store before publication. Corrupt/newer files and
external edits are preserved; writes stop with a visible explanation. A transient
save failure keeps session rows and can retry. QSaveFile/permissions and a writer
lock protect successful updates. Remove all saves an empty collection. Definition
changes do not persist poll counters, results or secrets. Bounds are 10,000 rows
and 16 MiB. Release-tree validation rejects the store and lock filenames.

Problems exports copy values from the published report; filtering never changes
Library/Profile/parser ownership. Severity item IDs remain scoped to the current
report. Rebuilds clear selection/details, and no QModelIndex survives a reset.
All modal workflows copy IDs/values before entering their nested event loop.

Review found and fixed two important details: extra Watch actions needed a
separate row to fit a narrow Query pane; native SNMP++ key/password argument
formats needed payload redaction before entering the shared logger. Regression
checks cover parenthesized keys, unquoted/multiline secrets, literal markup,
worker delivery, destroyed widgets and logger restart. Parser output/severity
and historical golden expectations were not rewritten.

## Validation

Initial per-item focused builds/tests passed, including actual Preferences,
Watch interval/save/restart, clipboard and file-chooser workflows. Representative
storage testing restores 150 rows and covers corrupt/unsupported files, external
edits, failed saving/recovery, deletion, secret exclusion and no startup requests.
Final validation completed after resuming the interrupted checkpoint:

- Focused workflows: **13/13 passed**, including the actual Preferences, saved
  Watch restart/interval actions, Problems chooser/export and native logging paths.
- Complete deterministic Debug suite: **65/65 passed**
  (1011.38 seconds), including all **27 tolerance scenarios** and
  **5/5 libsmi goldens**. Historical expected diagnostics were unchanged.
  The final 3,000-object Walk finished in 1.952 seconds (longest UI stall 56 ms);
  Stop completed in 32 ms, with no observed responsiveness regression.
- AddressSanitizer workflows: **13/13 passed** with leak detection disabled for
  the known full-application leaks; **10/10 isolated component checks passed with
  leak detection enabled**. The scripted loopback Watch workflow also passed ASAN.
- Release build/install, installed startup/restart at normal and 150% scaling,
  and the installed scripted loopback Watch workflow passed. The executable and
  all **355 installed MIB/PIB files** match their build/source bytes. All **401
  source MIB/PIB files** are unchanged. `ldd` found no missing libraries.
- Release-tree validation passed and rejected both task-owned Watch-store and
  lock-file packaging fixtures. The fixtures were removed and the clean tree
  reverified. Source hashes match all **1,944 implementation files** used for validation.
- `git diff --check` and LF/mixed-line-ending checks passed.

Review identified a test setup defect: the new Watch compact-window check kept
an expanded shared panel open and resized before Qt had updated its dock minimum.
It now uses the same collapsed-panel workflow as the existing compact-workspace
checks, lets the layout settle, verifies all four Watch action buttons, and
restores the prior panel/window state. The test and installed normal/150% runs
pass. No additional product defect was found during this resumed review.

Two initial build tool sessions were terminated with signal 143 before completing;
no compiler failure was reported. A detached driver with persistent stage results
completed the full validation. This was an execution interruption, not evidence of
a timing flake. No unresolved test failure or timeout remains. Exact stage statuses,
source hashes, detailed full-suite results and compiler warnings are in the
`resumed` evidence directory below.

The complete follow-up changes **35 expected source/documentation files**; this
resumed pass changes only the existing headless workflow test and this validation
record. No scope guardrail or unexpected expansion occurred. Personal untracked
files remain untouched; no generated repository `manual-validation/` output was
modified. Nothing was committed or pushed.

Existing Qt deprecations/unchecked returns and legacy full-app/libsmi leaks are
separate known limitations. Broad ASAN uses leak detection disabled; strict
isolated components retain leak detection. Windows/macOS require native CI; no
push is made to trigger it. Query Macros, Watch columns/tables, keychain storage,
strong parser cancellation and other unselected backlog items remain deferred.

## Artifacts and owner UX review

- Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`
- ASAN build: `/home/synoptics/src/snmpb-next/build-linux-asan`
- Release build: `/home/synoptics/src/snmpb-next/build-linux-release`
- Validated persistent install: `/home/synoptics/mib-navigator-validation/autonomous-five-2026-11-07`
- Validated executable: `/home/synoptics/mib-navigator-validation/autonomous-five-2026-11-07/bin/mib-navigator`
- Final evidence: `/home/synoptics/mib-navigator-validation/autonomous-five-2026-11-07-evidence/resumed`
  (the parent directory retains the earlier interrupted checkpoint).

**Ready for owner UX review.** The validated install dependency check passed;
Windows/macOS still require native CI. No desktop GUI or physical device was used.
Minimum UX review is: inspect the context
setting; create a watch, change its interval and restart to see it paused; filter
Problems and copy/export its CSV; inspect the readable diagnostic lines. No
manual debugging of persistence/parser/lifetime details is required.
