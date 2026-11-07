# MIB dependency consistency and responsiveness validation — 2026-11-03

The nonvisual MIB milestone now includes the reported Check Dependencies UI
freeze. Menus/Preferences and Traps remain separate upcoming milestones.

## Behavior and architecture

Check Dependencies checks the entire Library. Discovery, fresh semantic
verification and inventory preparation run on the existing serialized MibEngine
worker. The UI returns immediately, shows Checking, and allows navigation,
filtering and selection. Import, root changes, downloads and overlapping checks
are paused. Editor verification explains that it must wait. Profile changes and
refresh requests are remembered without waiting on the parser.

The worker owns an index copy and copied Library/catalog inputs. The UI keeps
reading its published index and immutable Profile environment. Completion
publishes one index snapshot under a short generation-check mutex and updates
the Library once from prepared inventory, avoiding duplicate synchronous rescans.
Context-bound deliveries and QPointer guards protect late callbacks; module
shutdown drains queued work before parser ownership is released. No parser-owned
pointer or QModelIndex survives a reset in the new code.

Uncached checking uses a fresh parser and Library-only search paths. Equivalent
raw-content duplicate providers remain accepted; different-content providers and
failed partial modules do not become verified through an already-loaded identity.
Check roots come from the refreshed index, so added and removed files participate.
The selected Profile remains the authority for Tree, Inspector, Query and Watch.
After publication, a forced build restores the latest requested Profile. An
unchanged cached check does not reset that parser. Profile generation checks are
protected against asynchronous index publication.

A final worker scan covers changes made while parsing or preparing inventory.
If files changed, the old semantic report is invalidated, discovery is published,
and a retry is requested. Deferred refreshes use that scan instead of blocking
again on a main-thread rescan. Index write failures preserve the previous check
record and remain retryable. Checker-policy versioning invalidates old shared-
parser reports while preserving provider graphs, saved reports and MIB bytes.

The Library index, profile plans and actual parser inventory were reviewed for
conflicting ownership. Legacy MIB search paths remain for old tools; they do not
feed the fresh Library checker or isolated Profile builder. Their retirement is
still a separate traced change. Patched libsmi/parser/scanner behavior is unchanged.

## Validation

Focused tests: **9/9 passed**. Complete permitted deterministic Debug suite:
**64/64 passed** in **903.62 seconds**, including **27** application/tolerance
scenarios and **5/5** historical libsmi goldens.

The actual Library button workflow uses 355 bundled MIB/PIB files and synthetic
products, with 358 physical files at its largest point. Its nine checks (eight actual button actions plus one deterministic worker-phase
file-change injection) cover
add/remove, equivalent providers, changed syntax, cached repeat, profile switching
while busy, deferred refresh, failed index save/retry, and a file added during
checking followed by retry. It compares the published Profile and actual parser
inventory after restoration. All **53 assertions** passed in Debug, installed
Release and ASAN. UI timers release a deliberately queued worker gate, proving
that the action returns and browsing executes while work is pending. Maximum
UI-timer gaps: Debug **1350 ms**, installed Release
**675 ms**, ASAN **1744 ms**.
These are measured fixture results, not a latency guarantee for arbitrary collections.

Widget controls, success/error completion and completion after widget destruction
passed ASAN with leak detection enabled. The full application workflow passed
ASAN invalid-access checks with an explicit 6-second instrumented display-latency
budget and leak detection disabled for the documented legacy
libsmi/UI leaks. This is not a leak-free full-application result.

Autonomous validation found and fixed a second completion-time stall: two Library
rescans blocked the UI for 3.4–4.9 seconds after the parser finished. Background
inventory preparation and one publication removed it. The next run also exposed
a deferred-refresh path causing a 2.35-second stall; it now uses the worker's final
scan. Sanitizer validation found a test defect: a timed file copy could race the
initial scan. The fixture now changes files deterministically after verification
and before the final scan, with timeout-safe completion state. An outdated source-text assertion expected the removed pre-scan module-list
argument and was updated; real button workflows now verify the behavior.
A Release run during sanitizer compilation exceeded the 1.5-second display
budget once (1,527 ms); its isolated rerun passed all workflows (633 ms maximum).
ASAN instrumentation exceeded that normal-build budget (1,987 ms maximum), so
memory validation now explicitly uses a 6-second display budget. Normal Debug
and Release retain 1.5 seconds. All builds retain the queued-worker UI gate and
500 ms action-return check. These are resource/instrumentation timing limits,
not suppressed sanitizer findings or evidence labeled as a timing flake.
No final focused/full-suite failures or timeouts remained.

Release build/install, installed offscreen startup/restart, installed dependency
workflow, executable byte comparison, all **355** installed MIB/PIB byte comparisons,
and `ldd` passed. All **401** repository MIB/PIB-tree files remain byte-identical
to the initial baseline. `git diff --check` and LF checks passed. Thirteen task
files include the earlier consistency work; no file-scope guardrail or unexpected
scope expansion occurred. Personal untracked files were preserved. No commit or
push was made.

## Remaining limits and manual handoff

Cold verification can still take time, but it no longer waits on the UI thread.
There is no cancellation/progress-count UI for dependency checking yet; shutdown
waits for serialized work to finish. Final widget publication remains synchronous
and was measured above with 358 files. Module metadata requiring a new parser
load is deferred while checking; file and dependency browsing remain available.

Existing Qt deprecations, obsolete UI tab-stop notices, unchecked `setuid` warnings
and previously documented application leaks remain visible. Windows and macOS
need GitHub Actions CI verification. No desktop GUI was launched and no physical
device was contacted; automated GUI workflows use the offscreen platform.

Ready for manual UX validation:

1. Start the executable below.
2. Run Library → Check Dependencies and switch tabs or select another MIB while it runs.
3. Confirm the window stays responsive and the selected Profile/Tree is correct afterward.

Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`.
Release build: `/home/synoptics/src/snmpb-next/build-linux-release`.
Install: `/tmp/mib-navigator-manual-validation/dependency-responsive`.
Executable: `/tmp/mib-navigator-manual-validation/dependency-responsive/bin/mib-navigator`.
Install dependency check: **passed; no missing libraries**.
