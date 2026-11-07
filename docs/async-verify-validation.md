# Asynchronous Editor verification validation

2026-11-04. Implementation is ready for manual UX validation.

## Behavior and ownership

Verify submits copied editor text and a Library index snapshot to the existing
serialized MibEngine worker and returns immediately. Library folder enumeration
also happens on that worker. The Editor shows running/completion state, prevents
duplicate checks, and remains editable. Changing text or opening/saving another
file invalidates the result; stale diagnostics are discarded with a request to
verify again. Verification failures retain their available structured diagnostics.

MibModule owns the job and parser coordination. Library metadata uses the
published inventory while checking; refresh and profile requests defer, and a
second Library check is rejected with an explanation. Completion applies any
deferred discovery and restores the latest requested Profile. The published
immutable Query Environment remains usable throughout. Verification's private
index is never published or persisted over the Library index. No parser policy,
provider policy, MIB bytes, profile membership or legacy preload ownership changed.
A changed Library location invalidates verification's old-context result.

The Editor now has QObject ownership under Snmpb. Worker jobs hold copied value
inputs, never widget or parser-node pointers. Completion uses the owner's QObject
context. Application shutdown drains the worker while its owner still exists and
ignores queued deliveries afterward. Parser operations cannot safely be
interrupted: closing during a long parse may wait for it to finish.

## Review and regressions

Review caught two alternate stall/state paths: synchronous metadata/refresh access
to the busy parser, and tab activation re-enabling Verify during a running check.
Both are covered by the real application workflow. An obsolete source-text
assertion named the old synchronous restoration entry point; it was removed
because restoration is now checked through actual profile-switch workflows. The
remaining architecture assertions stay in place. No product test failure was
left unresolved. An ad-hoc packaging comparison initially assumed a flat source
directory; correcting the source mapping verified the actual installed files.

A deterministic worker gate is released by a UI timer: a synchronous Verify
would block that timer and fail the responsiveness assertion. Both changed and
unchanged documents are checked. The workflow switches MIB subtabs, rejects
repeated Verify and concurrent Library checks, queues a Library refresh, changes
Profiles, waits for the correct noncached restoration, and checks exactly one
completion. It then queues Editor Verify immediately before shutdown and checks
that late completion/new work are ignored. Existing open/cancel chooser and
Library-only filename-mismatched import checks remain in the workflow.

## Results

- Final focused checks: 10/10 (nine engine/Library/application/golden checks plus
  the inventory architecture regression). Earlier related Environment/consumer
  checks also passed. Gated Verify clicks returned in 0–1 ms in observed runs.
- Complete deterministic suite: 65/65, 1113.85 seconds, including the 27
  application/tolerance scenarios and all five libsmi goldens.
- Goldens: SNMPv2-MIB, IF-MIB, MAU-MIB, RMON2-MIB, BRIDGE-MIB: 5/5.
- AddressSanitizer: 4/4 (engine, Environment manager, Library and actual app
  startup/restart, including Verify/shutdown); 95.72 seconds.
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 retains the documented existing
  application/libsmi leak limitation. This verifies invalid access, not absence
  of leaks.
- Release build/install, installed startup/restart workflows, release-tree check
  and ldd passed. No missing libraries. All 355 installed MIB/PIB files match
  source bytes; all 401 repository MIB-tree files match the earlier baseline.
- Frozen source hashes, LF checks and git diff --check passed.
- Existing Qt deprecations, unchecked setuid/QFile return-value warnings,
  obsolete UI tab-stop notices and offscreen-plugin notices remain visible.
  No warning was suppressed. No product test flake or timeout remained.
- No desktop GUI or physical-device SNMP operations were run. Windows/macOS
  changes require GitHub Actions CI; no commit or push was made.
- Nine task files: seven source/test files and two documents. No file-scope
  guardrail or unexpected expansion was triggered. Existing approved uncommitted
  work and personal workspace files were preserved.

## Artifacts and minimum manual check

- Debug: /home/synoptics/src/snmpb-next/build-linux-debug
- ASAN: /home/synoptics/src/snmpb-next/build-linux-asan
- Release: /home/synoptics/src/snmpb-next/build-linux-release
- Install: /tmp/mib-navigator-manual-validation/async-verify
- Executable: /tmp/mib-navigator-manual-validation/async-verify/bin/mib-navigator

Open a MIB in MIBs > Editor, click Verify, and switch tabs while it runs.
Confirm the window remains responsive and the completion message appears.
Automated checks cover the remaining concurrency and shutdown cases.
