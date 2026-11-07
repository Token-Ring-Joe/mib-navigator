# Query Macros v1 — agreed design

Prepared and agreed by the owner 2026-11-07. Status: **agreed design; implementation remains deferred**.
This document makes the 2026-11-04 direction concrete. The owner agreed the v1
scope and estimate during the finishing review; implementing it is a later task. The current temporary Multiple Varbinds dialog
remains until its tested replacement is accepted.

## User workflow

1. In Query, open a local **Macros** tab beside Results and Watch.
2. Create a named collection, such as MIB-II Essentials or Critical Drops.
3. Add scalar objects, table columns/groups, or exact numeric instances from the
   current MIB tree or an object chooser. Rename, duplicate, reorder and delete
   collections and entries. Editing definitions sends no requests.
4. Choose Target, Credential and MIB Profile in the connection bar and press Run.
5. If a table group uses **Choose when run**, choose its instances from the
   existing sparse-aware instance discovery. Cancel leaves previous results intact.
6. Inspect grouped results, raw values and MIB warnings; copy a group as CSV.
   Stop keeps completed and partial results with a clear stopped state.

Macros describes **what to read**. It saves no device, target history entry,
credential, secret, MIB Profile or polling schedule. Running is always explicit.
There is no automatic execution on startup, tab selection or connection changes.

## Selection and grouping

| Entry | Saved meaning | Run behavior |
|---|---|---|
| Scalar | Numeric object OID, scalar kind and optional qualified display name | Read its `.0` instance with an exact GET |
| Exact numeric instance | Complete instance OID and optional label | Exact GET; never guess or append an instance |
| Table group | Row OID, selected column OIDs and instance policy | Grid with decoded indexes and one column per selected object |

A table group has one of three instance policies: **All instances**, **Choose
when run**, or **Fixed instances**. Fixed suffixes accept compound numeric indexes
and remain exact: a vanished row must not become its successor. Prompt selection
may select several instances. The dialog shows decoded indexes when available and
always retains the raw suffix. A canceled prompt cancels the pending run; discovery
requests already completed are logged, but retrieval does not start.

Columns share a grid only when they share a row/index definition, including a
provable AUGMENTS relationship. Unrelated indexes remain separate groups even
when their numeric suffixes look alike. If the relationship cannot be established,
keep separate groups and explain it. Selected columns need not include an
inaccessible INDEX object; retain existing sparse-row behavior.

Numeric OIDs are the durable identity. Saved symbolic names are labels, never a
reason to silently change the requested numeric OID. A missing definition in the
chosen Profile permits raw numeric results using the explicit saved scalar/exact
instance semantics; it does not activate another Profile or load extra MIBs. If
current metadata contradicts an entry's saved object kind, report that entry for
review and continue valid entries. An unknown numeric object can be added only
as a complete exact-instance entry; no scalar or table inference is offered.

## Running and results

Each run captures a value copy of the macro definition, resolved request settings
(including Target and Credential), and the published immutable MIB Environment.
Later connection-bar changes, credential edits, Profile rebuilds, or macro edits
cannot retarget or reinterpret that run. A subsequent run uses the current choices.
An invalid connection prevents network work and leaves the previous results intact.

V1 allows one foreground Query operation or macro run at a time, using the existing
busy/Stop ownership. Existing Watch rows continue independently. Prompt discovery
belongs to that same operation and uses the captured context. Closing the Macros tab
is not a cancellation mechanism; its running state remains reachable in Operations.
Application shutdown cancels and drains work using the existing worker lifecycle.

Scalar/exact-instance entries use batched GETs. Table groups use the established
GETBULK engine for SNMPv2c/v3 and GETNEXT engine for SNMPv1, including tooBig
fallback, row/request bounds and cancellation. Fixed instances use exact GETs.
Groups execute sequentially; scalar batching is bounded and adapts to tooBig.
Reuse SNMPv1 noSuchName handling to isolate unsupported objects rather than discard
an otherwise valid group. Keep existing SNMPv3 REPORT explanations.

Results identify the macro, captured Target/Credential display name/Profile,
start time and completion state. Scalar/exact reads form one object/value group;
each independent table has its own grid. Received types, raw/interpreted values,
units, missing cells and value warnings use the existing renderer and checker.
A failed entry/group has an explanation and valid independent groups continue;
a run with mixed success is Partial. Stop prevents additional groups/requests and
preserves values already received. Authentication/security refusal ends the run
with its explanation rather than repeat the same rejected login for every group.

An explicit Run selects Results; background completion never changes workspace,
selected group or focus. Show group progress without clearing unrelated history.
Log each actual request through the shared request log, with macro/group context
and no secrets. CSV exports the selected group's visible rows and columns using
the existing CSV formatting rules. Saving/exporting an entire result session is
outside v1.

## Ownership and persistence

- A non-widget macro service owns definitions, run planning and lifecycle. Widgets
  project this state; the old modal dialog's `vblist` is not authoritative.
- Store a separate, versioned `query-macros-v1.json` under the application's existing
  private data/config location. Confirm the exact established location at
  implementation; do not rename QSettings roots or place state in the MIB collection.
- Persist stable IDs, names, ordered entries, numeric OIDs, optional display labels,
  and explicit instance policies/suffixes. No `SmiNode*`, QModelIndex, provider pin,
  copied MIB schema, credential reference or network endpoint belongs in a definition.
- Save atomically with QSaveFile. A failed save leaves the last good file and working
  edits available with a clear error. Loading malformed/newer files preserves the
  bytes and reports the problem; it must not replace them with an empty collection.
- Library/provider/dependency ownership, Automatic and Custom Profiles and published
  Environments remain unchanged. Creating/running a macro never edits MIB membership.
- Reuse `SnmpRequestContext`, `SnmpSingleOperation`, `SnmpTablePlan`,
  `SnmpTableOperation`, `tableindex.*`, the value renderer/checker and CSV formatter.
  Introduce grouped presentation values where needed, without retaining parser pointers.

The current `Agent::RunSingleRequest` also clears/presents Results and owns the
foreground busy state. Integrate macro orchestration through that ownership boundary;
do not invoke it repeatedly as a shortcut to scheduling groups. Extract shared
non-widget coordination only where needed. Current single-object SET stays available.
The old Multiple Varbinds UI, including its write-oriented New/Edit path, retires
only after the replacement milestone passes acceptance; multiple-object SET is
outside the agreed v1 scope.

## Estimate and build sequence

This is an engineering effort estimate, not a delivery-date or agent-runtime promise.
Allow **6–10 focused development days**, plus owner UX review and platform-CI time.
The main uncertainty is sharing foreground-operation ownership and presenting several
independent table groups without disturbing existing Query/Stop workflows.

| Milestone | Scope | Estimated effort |
|---|---|---|
| 1 | Definition model, safe repository, Macros list/editor, offline creation/restart workflows | 1–2 days |
| 2 | Captured run context, scalar/exact GET batches, grouped Results, Stop and Log | 1–2 days |
| 3 | Table grouping; all/prompt/fixed instances; sparse, AUGMENTS and failure handling | 2–3 days |
| 4 | End-to-end regressions, lifetime/performance review, old dialog retirement and release validation | 2–3 days |

Each milestone remains a working build, stays below the 50-file task limit, and
receives focused/full deterministic checks and a validated manual install. No
implementation starts as part of this finishing pass. Re-estimate if scheduled
runs, Watch integration, saved targets/credentials, macro import/export, SET,
expressions, cross-table joins or saved result sessions are added.

## Acceptance evidence required for implementation

- Create, duplicate, reorder, edit, delete and restart; preserve definitions on
  failed writes, corrupt files and unsupported schema versions.
- Mixed scalar/table/exact entries; unknown numeric OIDs; scalar `.0`; compound,
  string/IP and IMPLIED indexes; sparse and AUGMENTS rows; unrelated-index separation.
- SNMPv1 noSuchName, v2c/v3 batching, tooBig, missing objects, partial replies,
  refused login, timeout and bounded/stuck agents through scripted transport.
- Change Target/Credential/Profile and edit/delete definitions during discovery
  and execution; the captured run stays unchanged. No implicit network requests.
- Cancel the picker, Stop during each phase, close/hide tabs and shut down safely;
  delayed completions cannot modify a subsequent run or a destroyed widget.
- Exercise real menus/editor/pickers/Results/Log and restart, not only helper
  functions. Include large definitions/tables, CSV, ASAN, all five libsmi goldens,
  Release install/dependency checks and Windows/macOS CI.

Owner agreed on 2026-11-07: the one-shot read-only scope, Query-local Macros tab,
three table-instance policies, grouped Results, eventual retirement of the old
Multiple Varbinds UI, and the four-milestone estimate above.
