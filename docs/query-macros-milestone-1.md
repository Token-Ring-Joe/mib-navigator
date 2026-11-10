# Query Macros milestone 1 — checkpoint and working notes

Historical milestone: the owner authorized [milestone 2](query-macros-milestone-2.md)
on 2026-11-08. The scope and validation below describe milestone 1 only.

Owner authorized milestone 1 on 2026-11-07 after the maintenance status review.
Scope: definition model, safe repository/service, Query-local Macros list/editor,
and offline creation/edit/restart workflows. Execution, grouped Results, table
retrieval and old Multiple Varbinds retirement belong to later milestones.

## Checkpoint before implementation

- Local working clone: `/home/synoptics/src/snmpb-next`, branch
  `modernize/qt6-cmake`, HEAD `02a5f45`. The NAS tree is archival.
- Prior autonomous-five and maintenance-five changes remain intentionally
  uncommitted and approved. Preserve all of them and personal `.claude/` and
  `snmpb-next.code-workspace` files. No commit/push/tag/publication is authorized.
- Last validated maintenance checkpoint: focused Debug 22/22, Release 10/10,
  full deterministic suite 67/67 (27 scenarios, five libsmi goldens), ASAN
  invalid-access 16/16 and strict leak checks 11/11; diff check passed.
- Release build: `/home/synoptics/src/snmpb-next/build-linux-maintenance-release`.
- Stable install: `/home/synoptics/mib-navigator-validation/maintenance-five-2026-11-07`.
- Executable: `/home/synoptics/mib-navigator-validation/maintenance-five-2026-11-07/bin/mib-navigator`.
- Install/archive dependency checks passed; all 374 files matched, 355 installed
  MIB/PIB resources matched source/build, all 401 source hashes were unchanged.
- Details and archive paths: [maintenance validation](maintenance-five-validation.md).
  Measured parser leaks remain (14,567 bytes in that workflow), vendor compiler
  warnings remain visible, and native Windows/macOS CI/desktop/lab/distribution
  gates remain outstanding. No desktop GUI or physical-device SNMP was run.
- The preceding 51-file exception applies only to that completed batch. This
  milestone has a fresh baseline and the normal 50-file guardrail.

## Milestone ownership and intended workflows

The agreed [design](query-macros-v1-design.md) governs this implementation.
A non-widget service owns ordered definitions. Widgets edit copies and submit
validated values; neither legacy Varbinds nor the active MIB Profile owns macros.
Persist only stable IDs, names, numeric object/instance identities, optional labels,
and table instance policies. Use immutable Environment metadata only while choosing
objects; save no parser/model pointers, endpoints, credentials, Profiles or MIB schema.

Creation, duplication, entry editing/reordering, deletion and restart are offline.
Malformed/newer stores stay untouched and cannot be silently replaced. Atomic save
failures retain unsaved working edits and the last good file. Concurrent/external
changes must be detected. UI selections survive refresh by IDs rather than indexes.
No new SNMP execution or automatic requests are part of milestone 1.

## Implemented (ready for owner UX review)

- `QueryMacroService` (non-widget) owns working and committed definitions;
  `QueryMacroRepository` loads/saves `query-macros-v1.json` (see
  [settings schema](settings-schema.md)). `querymacro.*` holds the value model,
  numeric-OID canonicalization, validation and the metadata-only entry builder.
- Query > **Macros** tab and View > **Query Macros**: New, Duplicate, Delete,
  Up/Down, Name; entries via **Add object…** (a frozen, network-disabled tree
  picker), **Add exact instance…**, Edit, Duplicate entry, Remove, Up/Down; table
  groups edit ordered columns and All/Choose when run/Fixed instances. Save and
  Reload are explicit; closing the window with unsaved edits asks Save/Discard/Cancel.
- MIB tree context menu **Add to Selected Macro** adds to the selected macro (or
  a new one), merging same-row columns into the existing table group, and shows Macros.
- No requests are sent by any of these actions; Profiles and the request log
  are unchanged. Running macros is milestone 2.

Review fixes during validation: adding from the tree while the store is
preserved read-only now explains why nothing was added; column captions use the
same current Environment as the picker; the menu text no longer implies a dialog.
Added regressions: maximum-size (1024-entry) macro rename responsiveness and
selection retention, refused addition on a preserved newer-version store, and the
real tree action merging a column in the application smoke.

Known limitations for later milestones: after an external-change conflict the
only recovery is Reload (discarding working edits) — there is no merge/save-as;
the legacy Multiple Varbinds dialog remains until milestone 4.

## Validation (2026-11-07)

- Scope: 31 files changed relative to the milestone baseline (guardrail 50).
- Debug focused: query-macros, query-macros-panel, workbench-shell, launch-smoke 4/4.
- Full deterministic Debug suite 69/69 (912 s, 27 tolerance scenarios, five libsmi goldens).
- ASAN: macro/panel/workbench 3/3 with leak detection on; launch smoke 1/1
  (leak detection off for the documented parser leaks).
- Clean Release build `/home/synoptics/src/snmpb-next/build-linux-query-macros-m1-release`:
  focused 11/11 (goldens, release-tree policy, macros, smoke). Only the known
  SNMP++ format and generated libsmi parser const warnings appear.
- Install `/home/synoptics/mib-navigator-validation/query-macros-m1-2026-11-07`:
  release-tree verification passed, `ldd` reports no missing libraries, 333 MIBs
  and 22 PIBs match source bytes, and two headless installed runs save and restart
  the macro store (mode 0600, no stale lock).
- `git diff --check` passed. No desktop GUI, Windows/macOS CI or SNMP device was used.

Evidence: `/home/synoptics/mib-navigator-validation/query-macros-m1-2026-11-07-evidence`.
