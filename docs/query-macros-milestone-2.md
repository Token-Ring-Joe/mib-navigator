# Query Macros milestone 2 — captured scalar/exact runs

Owner authorized milestone 2 on 2026-11-08 after the project resume review.
Scope: captured scalar/exact-instance reads, bounded GET batching, grouped
Results, Stop, request logging and foreground Query integration. Table retrieval
and old Multiple Varbinds retirement remain milestones 3 and 4. Single-object
SET is unchanged. No commit, push, publication, desktop GUI or physical-device
SNMP is included. The prior 93-entry dirty worktree is preserved; the ordinary
50-file task limit applies to this milestone's changes.

## Ownership and user workflows

- The definition service/repository continue owning saved and working macros.
  Schema v1 is unchanged; execution does not implicitly save working edits.
- `QueryMacroRunner` owns captured run state and batch scheduling outside widgets.
  `Agent` reserves the existing foreground Query context for its whole lifetime.
  Repeated calls to the ordinary query entry point do not schedule macros.
- Run captures definition values, resolved target/port/credentials/context and
  the published immutable Environment. Results identifies the captured macro,
  Target, Credential display name, Profile and start time. Edits/deletion or
  later connection/Profile changes cannot replace or reinterpret that run.
  If the selected Profile has not published yet, Run explains why it cannot
  start and preserves prior Results. Offline sessions cannot create a transport.
- Scalars read `.0`; exact numeric instances remain exact. Missing metadata
  permits raw reads. A currently defined non-scalar/read-inaccessible object
  saved as a scalar is explained and skipped without blocking valid entries.
- GET batches start at eight entries and shrink after tooBig. SNMPv1 missing
  objects are isolated using a valid error-index; invalid indexes fail their
  batch safely. Missing/exception/mismatched replies are explained rather than
  silently becoming another instance. Extra values are ignored and reported.
- Independent failed batches do not discard later reads. Security refusal ends
  the run. Stop acknowledges cancellation immediately, keeps the in-flight
  response and prevents subsequent requests.
  The existing bounded single-request worker shutdown lifecycle remains in use.
- Results has an object/value group and separate explanatory placeholders for
  deferred table groups. It retains group/filter/display/selection on delivery,
  uses shared value checking, and exports the selected group's visible CSV.
  Explicit Run selects Results once; background deliveries never navigate.
- Actual batch requests, including adaptive attempts, use the shared request Log
  with macro/group context and explicit outcomes. Run state remains visible in
  Operations between batches and after clearing Log. Watch remains independent.

## Validation status

Autonomous validation completed 2026-11-08. The loopback workflow exposed Detect's
asynchronous publication boundary; the test now waits for publication and Run
itself refuses a mismatched pending selection without replacing Results.

- Complete Debug suite: 70/70 passed (875 s), including the 27 tolerance scenarios
  and the five unchanged libsmi goldens.
- Strict AddressSanitizer macro/runner/results/workbench checks: 5/5.
- Release: build, goldens, macro, Results, workbench, startup and policy checks 20/20.
- Runner tests cover malformed/imperfect replies, security refusal, invalid v1 error
  indexes, bounded all-missing isolation, extra values, Stop, the 1024-entry limit and
  destruction during an in-flight request. The application smoke covers the offline
  session (no transport, Results and Log unchanged); Results tests cover large captions.
- Install `~/mib-navigator-validation/query-macros-m2-2026-11-08`: executable
  byte-identical to the build, `ldd` reports no missing libraries.
- `git diff --check` passed. No desktop GUI or physical-device SNMP was used.

Known unchanged limitations: table groups do not execute yet; the old Multiple
Varbinds dialog remains; running requests cannot be interrupted while inside a
transport call; macro conflict recovery has no merge/save-as workflow. Existing
parser leaks/vendor warnings and native Windows/macOS CI gates remain.

The owner's separate ATM-TC-MIB question exposed the Library's uninformative
global Conflict label. It is recorded in [outstanding tasks](outstanding-tasks.md)
and is not a MIB-file repair or part of this execution milestone. Both user MIBs
were inspected read-only and left untouched.

## Follow-up: error output (2026-11-09)

Owner feedback found the run summary too terse ("Partial: 2 value(s), 1 problem(s)").
The summary now names up to three distinct causes with counts ("Problems:
2 × Timeout …"), and a timeout reads "no response from <endpoint> (timeout N s, M
retries). Check the Target address and port, that SNMP is enabled on the device, and
that the Credential is accepted." Covered by two new runner tests.
