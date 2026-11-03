# Task list: fixes from the 2026-11-01 technical assessment

This file is the prompt for a series of sessions. Each session does **exactly one**
item, the first one not yet ticked, then stops.

## How to use this file (read first, every session)

1. Read `AGENTS.md` and `docs/redesign-plan.md`. They govern this work. Work in
   `~/src/snmpb-next`, never in the NAS copy.
2. Find the first item below whose box is `[ ]`. That is this session's item. Do not
   start any other item, even if this one finishes early.
3. If an earlier item is ticked but its log says it is incomplete or blocked, stop
   and report that instead of continuing.
4. Re-check the "Known facts" for the item. Line numbers are approximate. If the
   facts are materially different (for example, the defect is already fixed), stop
   and report.
5. Follow the AGENTS.md development loop and build/test rules: write the failing
   test first, fix, focused tests, the complete suite, the five libsmi goldens,
   `git diff --check`, Release build, and install with an `ldd` check.
6. When the item is done, edit **this file**:
   - change its `[ ]` to `[x]`
   - fill in its **Log** with the date, files changed, test results (counts), and
     anything left unfinished or found along the way.
   Do not change other items' text.
7. Do not commit, push, stash, or reset. The user commits. The worktree may
   already contain uncommitted, approved work (for example the step 17 table
   work). Leave it as it is and build on it.
8. Finish with the AGENTS.md final report. Its first line must be:
   `Item N done: <one-line summary>. Next: item M.` (or `All items done.`).
   If a push-notification tool is available, also send a short notification
   saying the item finished and whether it is ready for manual validation.

Do not launch the GUI. Do not contact real devices. Use the scripted transport,
`app/fakesnmpagent.h`, and the 127.0.0.1 loopback agent
(`tools/loopback_snmp_agent.py`) only.

---

## [x] Item 1: Watch shows a false spike when a counter is cleared

**Known facts**

- `ComputeWatchChange` in `app/watchcalculator.cpp` (about lines 55-66) treats
  **any** drop in a Counter32 as a wrap when both samples carry sysUpTime:
  `(bothUptime || previous.counter >= kLikelyWrapFloor)`.
- Verified with a scratch harness: previous 1,000,000, current 500, sysUpTime
  rising by 10 s, octet counter. Result: change `+4293967796`, rate `3.44 Gbit/s`,
  note `Counter wrapped`. This is what happens after `clear counters` on a device
  that did not restart.
- `docs/redesign-plan.md` (Watch): "Counter wrap and agent restart must not show
  false spikes."
- `app/watchcalculator_test.cpp` tests a counter clear only **without** sysUpTime.

**Required behaviour**

- sysUpTime is used only to detect a device restart (uptime went backwards) and
  to time the rate. It is not evidence that a falling counter wrapped.
- A falling Counter32 counts as a wrap only when the previous value was in the
  top quarter of the range (`kLikelyWrapFloor`), whether or not sysUpTime is
  present. Otherwise: change `-`, note `Counter reset: baseline reset`,
  no rate, `baselineReset = true`.
- Counter64 behaviour is unchanged (any fall is a reset).
- Update the header comment in `app/watchcalculator.h` and the Watch text in
  `docs/redesign-plan.md` step 10 so they describe the rule correctly.
  Currently they say a wrap is told from a restart "by sysUpTime".

**Tests to add (before the fix, confirm they fail)**

- The harness case above: counter 1,000,000 → 500 with sysUpTime rising. Expect
  a baseline reset, no rate, and the reset note.
- A counter clear from a value just below the floor, with sysUpTime. Expect a reset.
- A genuine wrap with sysUpTime (already covered; must still pass with the same
  delta and rate).
- A restart (sysUpTime falls) still wins over everything else.
- If `app/watchservice_test.cpp` can drive a sequence of polls, add one sequence
  showing that the row after a counter clear has no rate and the next poll has a
  normal rate.

**Scope:** expected 3-5 files. Stop if more than 10.

**Log:**

- 2026-11-01. Files changed (5): `app/watchcalculator.cpp` (fix), `app/watchcalculator.h`
  (header comment), `app/watchcalculator_test.cpp`, `app/watchservice_test.cpp`,
  `docs/redesign-plan.md` (step 10 Watch text). Plus this file.
- Fix: a falling Counter32 is a wrap only when `previous.counter >= kLikelyWrapFloor`; sysUpTime
  is no longer part of that test. It still detects a restart and times the rate. Counter64
  unchanged.
- Tests added first and confirmed failing: 1,000,000 -> 500 with rising sysUpTime; fall from
  0xBFFFFFFF with sysUpTime. Also added: fall from exactly 0xC0000000 with sysUpTime is a wrap
  (+1073741834); just below the floor without sysUpTime is a reset; a restart wins over a fall
  from the top of the range; a WatchService poll sequence (baseline, clear, next poll has a
  normal +1000 / 200 /s).
- Results: `snmpb.watch-calculator` and `snmpb.watch-service` pass (Debug and Release); complete
  Debug suite 57/57 (includes `snmpb.tolerance-scenarios` and `snmpb.launch-smoke`); five
  libsmi goldens 5/5; `git diff --check` clean; Release build and install OK, `ldd` shows no
  missing libraries. The compiler warnings seen are the existing deprecated `QMessageBox::warning`
  overloads in `app/agent.cpp`; none are new.
- Nothing left unfinished. Windows/macOS: pure Qt-Core arithmetic, no platform code; CI only.

---

## [x] Item 2: SNMPv3 local failures shown as "SNMP error 0"

**Known facts**

- `classify()` in `app/snmptransport.cpp` (about lines 6-17) treats every
  positive SNMP++ return code as the device's SNMP error-status and marks the
  result `SnmpError`.
- SNMP++ USM error codes are **also positive**: `SNMPv3_USM_*` = 1400-1420
  (`snmp++/include/snmp_pp/usm_v3.h`, about lines 114-134). SNMP++ returns
  `SNMPv3_USM_DECRYPTION_ERROR` directly when a REPORT cannot be decrypted
  (`snmp++/src/uxsnmp.cpp`, the synchronous REPORT path after
  `SNMPBlockForResponse`). MP errors `SNMPv3_MP_*` are negative (-1400 to -1418)
  and already become `TransportFailure`.
- Real SNMP error-status values are 1-18 (`SNMP_ERROR_TOO_BIG` …
  `SNMP_ERROR_INCONSIST_NAME`, `snmperrs.h`).
- Because `pdu.get_error_status()` is 0 in those cases, users see
  "SNMP error 0: the device refused…" from `app/agent.cpp` (table result, about
  line 1582; instance list, about line 1900). Watch (`app/watchservice.cpp`) and
  Detect use the same transport. Trace every place that turns a
  `SnmpTransportResult` or `SnmpOperationStatus` into text.
- Related small defect, also in scope: `app/agent.cpp` (about line 437),
  `tr("Could not create v3MP object:\n").arg(...)` has no `%1`, so the reason is
  dropped. The code then calls `v3mp->get_usm()` even when creation failed. Check
  in SNMP++ whether `get_usm()` can be null there, and guard it if so.

**Required behaviour**

- Only a positive code in the SNMP error-status range (1-18) with a matching PDU
  error-status counts as the device's SNMP error. Positive codes outside that range
  (the USM range) are classified as a failure on this side, not as a device error.
  Choose the status (an existing enum value or a new one) after tracing every
  `switch` on `SnmpOperationStatus`. Keep `transportStatus` so the code is not lost.
- Users see a plain-English reason, never "SNMP error 0". At minimum:
  - decryption error: the privacy passphrase or algorithm likely does not match
  - unknown security name: the SNMPv3 user is not set up in this application's
    credentials
  - authentication failure or error: the authentication passphrase or algorithm
    likely does not match
  - anything else: SNMP++'s `Snmp::error_msg(code)` text with the code
  Match the tone of `app/snmpreport.cpp`. Consider putting the translation there,
  or beside it.
- The Log entry and the status line show the same reason.
- The v3MP startup message includes the reason.

**Tests to add (before the fix, confirm they fail)**

- `ScriptedSnmpTransport` or `FakeSnmpAgent` results with `transportStatus` set to
  `SNMPv3_USM_DECRYPTION_ERROR`, `SNMPv3_USM_UNKNOWN_SECURITY_NAME`, and
  `SNMPv3_USM_AUTHENTICATION_FAILURE`, run through the table operation, the
  instance operation, and the Watch service. Assert the status and that the
  user-facing text names the cause and contains no "SNMP error 0".
- A real device error (for example noSuchName = 2 with PDU error-status 2) is
  still classified exactly as before. The existing `snmpb.snmp-table` tooBig and
  noSuchName tests must pass unchanged.
- Unit-test `classify()` directly if you expose it (for example through a small
  internal header). Otherwise test through `SnmpPlusTransport` only if that needs
  no network.

**Scope:** expected 5-10 files. Stop if more than 15.

**Log:**

- 2026-11-01. Files changed (14 + this file): `app/snmptransport.h/.cpp` (new
  `ClassifySnmpTransportStatus`, replaces the private `classify()`), `app/snmpreport.h/.cpp`
  (`IsSnmpErrorStatusCode`, `ExplainSnmpTransportFailure`), `app/snmptableoperation.cpp`,
  `app/snmpinstanceoperation.cpp`, `app/watchservice.cpp`, `app/profiledetector.cpp`,
  `app/agent.cpp`; tests `app/snmpreport_test.cpp`, `app/snmpoperations_test.cpp`,
  `app/snmptable_test.cpp`, `app/watchservice_test.cpp`, `app/profiledetector_test.cpp`.
  Several of these already carried uncommitted work from earlier steps; this item's edits were
  added on top.
- Classification: a positive code is a device `SnmpError` only when it is 1-18 **and** the PDU
  error-status equals it (SNMP++ returns `pdu.get_error_status()` itself, `uxsnmp.cpp` ~438).
  Any other positive code (USM, 1400+) becomes `TransportFailure`, with `transportStatus` kept.
  No new enum value: every `switch` on `SnmpOperationStatus` (table, instance, Watch, Detect,
  `Agent::PresentTableResult`, `Agent::PresentInstanceResult`) already handles it.
- Text: `ExplainSnmpTransportFailure` (beside `ExplainSnmpReport`) names decryption (privacy
  passphrase/algorithm), unknown security name (user not set up in this application's
  credentials), authentication failure/error (authentication passphrase/algorithm); any other
  code gets `Snmp::error_msg` text plus the code. Table and instance operations put it in
  `problem`, which the presenters already show identically in the Query pane, status line and
  Log. Table presenter no longer adds "No response received" when a reason is present. Watch and
  Detect use it for their status/error text. Previously these showed "SNMP error 0" (table,
  instance) or "The request could not be sent (<SNMP++ text>)" for any code (Watch, Detect).
- v3MP startup message now includes the reason (`%1` was missing). `v3MP::get_usm()` can be null
  (the constructor leaves `usm` at 0 on the early "second v3MP" error), so `load_users` is now
  guarded.
- Tests added first; the table, instance, Watch and Detect cases were run and confirmed failing
  before the fix. The report-text and classification tests were added with the fix (they need
  the new API). Existing `snmpb.snmp-table` tooBig/noSuchName tests pass unchanged.
- Results: Debug `ctest` pattern run of 52 tests passed (including `snmpb.tolerance-scenarios`
  and `snmpb.launch-smoke`), plus the five libsmi goldens 5/5, i.e. all 57; Release focused run
  11/11 (snmp-*, watch, profile-detector, goldens); `git diff --check` clean; Release build and
  install OK, `ldd` shows no missing libraries. No new compiler warnings in touched files.
- Not changed, for later items: the legacy callback path in `Agent::AsyncCallback` (Get/Next/
  Bulk/Walk/Set) still formats SNMP++ codes itself and has not been checked for USM codes;
  items 3-5 move those onto this classification. `ScriptedSnmpTransport`/`FakeSnmpAgent` set
  `status` directly, so the classifier is tested on its own, not through them.
- Windows/macOS: no platform code; CI only.

---

## [x] Item 3: Move Walk off the legacy callback path

**Background.** Get, GetNext, GetBulk, Walk, and Set still run through SNMP++'s
async API on the GUI thread. `Agent::WalkFrom` (`app/agent.cpp`, about line 1161)
sends a GETBULK with `callback_walk`. A `QTimer` calls
`SNMPProcessPendingEvents()` (`Agent::TimerExpired`, about line 658), and
`Agent::AsyncCallback(..., iswalk)` (about lines 803-1003) classifies errors,
builds HTML, fills Results, and sends the next GETBULK. Progress is held in
`Agent` members (`requests`, `objects`, `msg`, `stop`, `activeRequestContext`).

Query Table, the instance picker, and Watch already use the newer design:
`ISnmpTransport` (`app/snmptransport.h`), an operation class with no widgets
(`app/snmptableoperation.cpp`), an async runner on a worker thread
(`app/snmptableasyncrunner.cpp`), and a presenter in `Agent`
(`Agent::PresentTableResult`).

**Confirmed defect this fixes.** The legacy Walk never checks that each returned
OID is greater than the previous one. It stops only on `endOfMibView`, leaving the
subtree, or noSuchName. An agent that returns the same or a lower OID inside the
subtree makes it loop until the user presses Stop. The table walker already has
this check (`Walker::absorb` in `app/snmptableoperation.cpp`).

**Required behaviour**

- A widget-free `SnmpWalkOperation` (new files) walks a subtree through
  `ISnmpTransport`:
  - GETBULK on SNMPv2c/v3, with max-repetitions from the request context.
    Non-repeaters are always 0 for a walk, ignoring the global setting.
  - GETNEXT on SNMPv1; noSuchName ends the walk.
  - Ends on endOfMibView, leaving the subtree, noSuchObject/noSuchInstance, or
    an empty response.
  - A non-increasing OID ends the walk with a plain reason and keeps the
    results so far.
  - tooBig halves max-repetitions, then fails cleanly.
  - An SNMPv3 REPORT is reported through `ExplainSnmpReport`.
  - Classification goes through item 2's transport mapping.
  - A cancellation token is checked between requests.
  - Request and object caps, at least as generous as the table walker's, stop a
    runaway walk with a reason.
- An async runner runs it on a worker thread. Results must still appear
  **progressively** during a long walk, not only at the end. Deliver batches
  through a queued signal and keep the GUI thread responsive.
- `Agent::WalkFrom` uses the new path. Stop cancels it. Shutdown during a walk
  must not hang indefinitely: bound the wait, and note how in the Log.
- Users see the same thing as today: Results rows, the Query pane text, the
  status line ("Finished: N objects in M requests", "Stopped after …"), and the
  Log entry with outcome, object count, and duration.
- Delete the walk branch of `AsyncCallback` and `callback_walk` once nothing
  uses them. **Keep** the timer and `SNMPProcessPendingEvents()`: traps and
  informs still need them, and so do Get/Next/Bulk/Set until items 4-5.
- `SnmpPlusTransport` creates its own `Snmp` session. Confirm this is safe beside
  the GUI-thread session and the trap listener (SNMP++ is built with `_THREADS`;
  there is one global v3MP).

**Tests**

- Unit tests with `FakeSnmpAgent` or `ScriptedSnmpTransport`:
  - a normal walk with v2c GETBULK
  - SNMPv1 with noSuchName at the end
  - endOfMibView
  - leaving the subtree in the middle of a GETBULK response
  - **a repeated OID and a lower OID inside the subtree, which must stop with a
    reason**
  - a REPORT
  - a timeout mid-walk that keeps the partial results
  - tooBig
  - cancel
  - caps
  - a 0-object subtree
- The existing loopback scenarios (`cmake/RunSnmpbToleranceScenarios.cmake`,
  `live-*`) must pass. `live-table-*` compares the table against "a plain Walk of
  the same subtree", which is the regression anchor. Before changing code, record
  the loopback Walk output for the subtrees those scenarios use. Afterwards,
  confirm the output is object-for-object identical.
- If practical, add a loopback-agent mode that answers out of order, and a live
  scenario proving Walk stops with the reason.

**Scope:** expected 8-15 files. Stop if more than 25, and report a split instead.

**Log:**

- 2026-11-01. Files (11 + this file). New: `app/snmpwalkoperation.h/.cpp` (widget-free
  `SnmpWalkOperation`), `app/snmpwalkasyncrunner.h/.cpp` (worker-thread runner),
  `app/snmpwalk_test.cpp` (`snmpb.snmp-walk`). Changed: `app/agent.cpp`, `app/agent.h`,
  `app/CMakeLists.txt`, `app/main.cpp` (smoke diagnostics only), `tools/loopback_snmp_agent.py`,
  `cmake/RunSnmpbToleranceScenarios.cmake`.
- Operation: GETBULK (v2c/v3, max-repetitions from the request context, non-repeaters always 0)
  or GETNEXT (v1). Ends on endOfMibView, leaving the subtree, noSuchObject/noSuchInstance,
  noSuchName, or an empty response. A non-increasing OID (equal or lower, also within one answer,
  also the starting OID) ends the walk with a plain reason and keeps the objects so far.
  tooBig halves max-repetitions down to 1, then fails with `SnmpError`. REPORT goes through
  `ExplainSnmpReport`; local failures through item 2's `ExplainSnmpTransportFailure`. Cancellation
  is checked between requests. Caps: 2,000,000 requests and 2,000,000 objects (the table walker's
  are 2,000,000 requests / 250,000 rows), each ending with a reason.
- Runner: objects go through a mutex-protected queue and one coalesced queued wake-up, so they
  appear while the walk runs, in order, and `completed` always follows the last `progress`.
  Back-pressure: the worker waits (checking cancellation every 50 ms) while more than 2000
  objects are undisplayed, so a fast agent cannot flood the GUI thread. `start()` now also
  accepts a new walk in the handler of the previous result (the old thread's cleanup captures its
  own thread pointer), and refuses one while a walk is in progress.
- Shutdown: `Agent::~Agent` deletes the runner, whose destructor cancels and waits at most
  `kShutdownWaitMs` = 3000 ms for a request still in flight (cancellation is only seen between
  requests), then detaches the thread: it deletes itself when its request returns and nothing it
  does afterwards reaches the Agent. Tested with a stuck transport (bounded wait 200 ms).
  Stop only cancels between requests, as the legacy path did.
- `Agent::WalkFrom` uses the new path; `callback_walk`, the walk branches of `AsyncCallback`
  (its `iswalk` parameter is gone) and the GETBULK re-issue are deleted. The timer and
  `SNMPProcessPendingEvents()` are kept (traps/informs, Get/Next/Bulk/Set until items 4-5).
  Presenters: `Agent::PresentWalkBatch` (Results rows + Query lines) and
  `Agent::PresentWalkResult` (Query text, status line, Log). Texts are unchanged ("Finished: N
  object(s) in M request(s).", "Stopped after ...", the Timeout text); a walk that ends early
  says "Ended early after N object(s) in M request(s)." followed by the reason, and is logged
  as Failed. Unknown-OID values in the Query pane are now HTML-escaped.
- Session safety: `SnmpPlusTransport` creates its own `Snmp` (own socket and event list, no
  shared state with the GUI-thread session or the trap listener); the one global v3MP/USM is
  synchronized in SNMP++ (`SnmpSynchronized`). The table runner and Watch already run this way.
- Defect confirmed before the fix (new scenarios `live-walk-out-of-order-*`, loopback agent
  subtree 90001.9 answering 9.1, 9.2, 9.1, ...): the legacy Walk ran until Stop, 3260 objects
  (v2c) / 754 objects (v1). After: both end by themselves with 2 objects and the reason.
- Loopback regression anchor: Results rows, request counts, Log result/objects and status text
  of the plain Walk over the `live-table-*` subtrees (2.1 and 3.1/4.1, v2c and v1) recorded
  before the change and compared after: identical, object for object.
- Tests: `snmpb.snmp-walk` covers the listed cases (v2c bulk, v1 noSuchName, endOfMibView,
  leaving the subtree mid-response, repeated and lower OIDs, REPORT, local SNMPv3 failure,
  timeout with partial results, tooBig, cancel, caps, empty subtree, plus the runner's order,
  progressive delivery, Stop, back-to-back start, and bounded shutdown). 30 repeated runs and
  ASan runs clean.
- Results: Debug full suite 58/58 (includes the 5 libsmi goldens, 5/5, and
  `snmpb.tolerance-scenarios`, which now takes about 570-590 s with 4 jobs running, so its
  ctest TIMEOUT was raised from 600 to 900 s); Release focused 12/12; `git diff --check` clean;
  Release build and install OK, `ldd` clean. No new compiler warnings.
- Known limits: Get/Next/Bulk/Set still use `AsyncCallback`/`AsyncCallbackSet` (items 4-5). The
  walk's status line after Stop appears only once the request in flight returns or times out.
  Windows/macOS: Qt threads and SNMP++ only; CI.

- Follow-up (2026-11-01, reported by the user after items 3-5): pressing Stop during a long Walk
  made the desktop say the application was not responding. Cause: `appendStructuredResult`
  switched the Results table's sorting off and on again for every row, which re-sorts the whole
  table each time, so cost grew with the square of the row count (measured: 8000 rows = 135 s of
  GUI time against 0.2 s when sorted once). The old Walk was paced by the network, which hid it;
  the worker delivers much faster, so the GUI thread stayed busy and could not handle the click.
  Fixes: the Results table stays unsorted while a query fills it and is sorted once at the end
  (`Agent::HoldResultSorting` / `ReleaseResultSorting`, also used by Query Table and the single
  requests); the runner now hands the GUI at most `kDeliveryChunk` = 250 objects at a time and
  emits `completed` only after the last chunk, so clicks are handled between chunks. New headless
  scenario `live-walk-large-and-stop` (loopback agent subtree of 3000 objects, `--smoke-live-bigwalk`)
  walks it, then presses the real Stop action: longest event-loop stall 2.4-3.4 s before the fix,
  about 150 ms after; Stop ends the walk in about 95 ms; the scenario fails above 750 ms. Unit test
  added for the chunk size and a running timer. Debug suite 59/59, Release focused tests and
  install OK.
---

## [x] Item 4: Move Get, GetNext, and GetBulk off the legacy callback path

Only start after item 3 is ticked and its log reports no open problems.

**Required behaviour**

- The same pattern as item 3, for single-request operations: a widget-free
  operation (or one operation with a mode), the async runner, and one presenter
  shared with Walk for Results, Query pane text, status line, and Log.
- All entry points keep working, including the varbind-list variants
  (`VarbindsGetBulk`, `GetBulk(oid, usevblist)`) and the instance-picker
  follow-ups that call `GetBulk` (`app/agent.cpp`, about lines 1944 and 2047).
- Users see the same output as today, including the per-varbind "No Such
  Object" and "No Such Instance" lines and the error-index highlighting.
- Remove `callback` and the remaining non-trap parts of `AsyncCallback` once
  unused. The timer stays for traps and Set.

**Tests:** unit tests per operation, covering success, noSuchObject/Instance, a
PDU error with error-index, timeout, REPORT, and the item 2 USM codes. All `live-*`
scenarios pass, and the loopback Get output is identical before and after.

**Scope:** expected 6-12 files. Stop if more than 25.

**Log:**

- 2026-11-01. Files (12 + this file; the estimate was 6-12, the limit 25). New:
  `app/snmpsingleoperation.h/.cpp` (widget-free Get/GetNext/GetBulk, one operation chosen by the
  request context), `app/snmpsingleasyncrunner.h/.cpp`, `app/snmpquerytext.h/.cpp` (widget-free
  text for the Query pane / status line / Log, shared with Walk), `app/snmpsingle_test.cpp`
  (`snmpb.snmp-single`). Changed: `app/agent.cpp`, `app/agent.h`, `app/CMakeLists.txt`,
  `app/main.cpp` (smoke diagnostics only), `cmake/RunSnmpbToleranceScenarios.cmake`.
- `Agent::Get/GetNext/GetBulk` (and so `GetFrom`, `GetFromPromptInstance`, the instance-picker
  follow-ups in `PresentInstanceResult`, and the Varbinds dialog's `VarbindsGet/GetNext/GetBulk`)
  all go through `Agent::RunSingleRequest`: the varbind list (one OID, or the Varbinds dialog's
  list) is read back from the PDU `ConfigPduFromSettings` built, so both variants share one path.
  The answer is shown by `Agent::PresentSingleResult`; Walk's `PresentWalkResult` and the single
  presenter both end in `Agent::FinishQuery` (Results rows, Query pane, status line, Log entry,
  re-enable controls). `FormatSingleResult` is a transplant of the legacy `AsyncCallback` loop,
  keeping its behaviour on purpose: per-varbind "No Such Object" / "No Such Instance",
  "ERROR on varbind #N" for the varbind a device error's error-index names (output starts at that
  varbind and numbering at the index), ten objects per Query paragraph, a known object ends the
  answer at its first error and an unknown one does not. Strings keep the "Agent" translation
  context. One deliberate change: values of OIDs the MIB does not know are now HTML-escaped.
- Removed: the C `callback`, `Agent::AsyncCallback` and its declaration. `AsyncCallbackSet`,
  `AsyncCallbackTrap`, the timer and `SNMPProcessPendingEvents()` stay (Set is item 5; traps).
- Errors: a REPORT and local failures use `problem` (item 2); a timeout keeps its text; a failed
  send reads "No response received".
- A second Get/GetNext/GetBulk while one is in flight is ignored (the old code ran both on the
  same shared state and garbled the output). Stop sets the `stop` flag, so a request answered
  after Stop is labelled "stopped", as before; a request already sent cannot be cancelled. Same
  bounded shutdown as Walk (3 s, then the worker finishes by itself).
- Loopback comparison: before changing code I recorded, for SNMPv2c and SNMPv1 through the
  connection bar, Get, GetNext, GetBulk, a missing object (v2c: this agent answers
  endOfMibView; v1: noSuchName naming the varbind), a GetNext past the end, and the dead-port
  timeout: Query pane text, status line, Results rows and Log entry for each. After the change the
  output is identical (timestamps and the agent's moving sysUpTime normalized). The scenarios
  `live-single-*` now pin those texts.
- Tests: `snmpb.snmp-single` covers operations (mapping, global non-repeaters/max-repetitions
  for a single GetBulk, varbind lists, noSuchObject/noSuchInstance, PDU error with error-index,
  timeout, REPORT, the three item 2 USM codes), the formatter (exact text, stop, paragraph
  flush, per-varbind errors for known and unknown objects, error-index highlighting, empty,
  endOfMibView, no response, the Walk ending texts) and the runner (result thread, refusal while
  busy, start from the result handler, bounded shutdown). 20 repeats and ASan clean.
- Results: Debug full suite 59/59 on the final code (includes the five libsmi goldens, 5/5, and
  `snmpb.tolerance-scenarios`, 650 s); Release focused 13/13; `git diff --check` clean; Release
  build and install OK, `ldd` clean. No new compiler warnings. The tolerance suite's ctest
  TIMEOUT is now 1200 s (was 600, 900): each live scenario costs about a minute of application
  start-up, and this item and item 3 added four. The Walk and single-request scenarios skip the
  dead-port/Log/Detect checks that the main live scenario already runs.
- Not covered end to end: the Varbinds dialog buttons (they need a dialog; they share
  `RunSingleRequest` with the tested entry points) and a real SNMPv3 device.

---

## [x] Item 5: Move Set off the legacy callback path

Only start after item 4 is ticked and its log reports no open problems. Set
writes to devices. Test only with the scripted transport and the loopback agent.

**Required behaviour**

- The same pattern for Set (`AsyncCallbackSet`, `callback_set`). Keep the current
  confirmation and value-entry UI. Error reporting names the failing varbind
  (error-index), and SNMP error-status names are shown in plain English.
- Afterwards, the SNMP++ event-loop timer serves only traps and informs. Confirm
  this and say so in the log.

**Tests:** success, each common Set error (notWritable, wrongType, wrongValue,
noAccess, inconsistentValue) with error-index, timeout, REPORT, and the item 2
USM codes. If the loopback agent supports Set, add a live scenario. If it doesn't,
say so in the log and do not extend the agent beyond 127.0.0.1.

**Scope:** expected 4-10 files. Stop if more than 25.

**Log:**

- 2026-11-01. Files (13 + this file; the estimate was 4-10, the limit 25): `app/snmpreport.h/.cpp`
  (`ExplainSnmpErrorStatus`), `app/snmpsingleoperation.h/.cpp` (Set mode), `app/snmpquerytext.h/.cpp`
  (`FormatSetResult`), `app/agent.cpp`, `app/agent.h`, `app/main.cpp` (smoke only),
  `tools/loopback_snmp_agent.py`, `cmake/RunSnmpbToleranceScenarios.cmake`, tests
  `app/snmpsingle_test.cpp` and `app/snmpreport_test.cpp`.
- Set reuses the item 4 operation, runner and presenter: `Agent::Set` -> `RunSingleRequest(Set)` ->
  `PresentSingleResult` -> `FormatSetResult` -> `FinishQuery`. `SetFrom` (the unchanged MibSelection
  confirmation / value-entry dialog) and the Varbinds dialog's Set still arrive at `Agent::Set`;
  `SetFrom` now calls the new public `Agent::SetVarbinds`, which the smoke run also uses. The write
  community and SNMPv3 settings come from the same request config as before.
- Output is the legacy Set output (header "SNMP set started", the response varbinds, "SNMP set
  finished", status line and Log detail = that text; no Results rows, as before; a Set does not
  touch the Stop button or the walking state), with the requested changes: the failing varbind is
  named through the error-index ("ERROR on varbind #N: <name>") and the device's error-status is
  explained in plain English by `ExplainSnmpErrorStatus`, e.g. "notWritable: this object cannot be
  changed (SNMP error 17)", for all of 1-18 (SNMPv1's badValue/readOnly/noSuchName wording is
  explicit about v1). Get keeps its old wording. Timeout, SNMPv3 REPORT and the item 2 USM codes
  use the shared handling.
- Removed: `callback_set`, `Agent::AsyncCallbackSet` and `ASYNC_TIMER_MSEC`. Confirmed with grep:
  the only remaining uses of the `Snmp` object in `Agent` are `notify_*` registration, `response`
  to informs and `SNMPProcessPendingEvents()` in `TimerExpired`, and the timer is only started at
  the trap rate (`TRAP_TIMER_MSEC`) by `StartTrapTimer`. So the SNMP++ event-loop timer now serves
  only traps and informs.
- Loopback agent: it did not support Set. It now answers SET (write community `smokerw`) in memory
  only and still binds 127.0.0.1 only: sysName.0 is writable (a string); a non-string for it gives
  wrongType (v1: badValue); goodValue.0 gives wrongValue; any other known object notWritable (v1:
  readOnly); an unknown object noAccess (v1: noSuchName). Scenarios `live-single-*` (SNMPv2c and
  SNMPv1) now run a successful Set, a read-back Get and the four refusals through the connection
  bar and check Query pane, status line and Log. No Set was sent anywhere else.
- Tests: `snmpb.snmp-single` (Set carries the values; success text; notWritable, wrongType,
  wrongValue, noAccess and inconsistentValue each with error-index, for unknown and known objects;
  error without index; timeout; REPORT; the three USM codes; Get wording unchanged) and
  `snmpb.snmp-report` (every error-status 1-18 explained). 20 repeats and ASan clean.
- Results: Debug full suite 59/59 (five libsmi goldens 5/5; `snmpb.tolerance-scenarios` 696 s of
  its 1200 s limit); Release focused 13/13; `git diff --check` clean; Release build and install
  OK, `ldd` clean. No new compiler warnings.
- Known limits: the MibSelection dialog and the Varbinds dialog buttons are not driven by tests
  (they feed the same `Agent::Set`). No pre-change comparison of Set text exists because the old
  loopback agent could not answer a Set; the success text mirrors the old format and the error
  wording is changed on purpose.
