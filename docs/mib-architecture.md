# MIB browser and loader architecture

## Current architecture — 2026-11-07

[Redesign Plan](redesign-plan.md) governs product direction; the sections below
this checkpoint retain the implementation history and may describe superseded
phases. Current responsibilities are:

- The user-visible collection and Library provider/dependency index own global
  knowledge. Automatic Profiles derive from Vendor/Product folders; Custom
  Profiles hold explicit user selections. Dependencies remain a Library concern.
- The effective plan authorizes actual providers. Pins retain precedence;
  applicable duplicates resolve by newest valid revision and stable path order.
  A bad root/import produces a Partial Environment with an actionable report
  while other authorized modules continue loading.
- Published Environments contain copied values. Query runs capture an immutable
  Environment; Watch interpretation uses the Environment selected for each poll;
  retained traps follow the active Profile. Presentation retains no parser-owned
  pointers across resets. Serialized parser work restores the latest Profile.
- Query, MIBs (Library/Profiles/Editor), Traps and the shared Operations panel
  replace old Modules arrows, Graphs, Discovery and saved-device screens. Legacy
  preload/search-path settings do not silently override the active plan.
- Shared rendering/checking supplies Results, tables, Watch and Traps. Standard
  textual-convention and DISPLAY-HINT checks warn about received values without
  refusing MIB loading or editing source files. Unsupported hints retain raw
  presentation. The new checks and narrow allocation-lifetime fixes are covered
  by [maintenance validation](maintenance-five-validation.md).

See [settings-schema.md](settings-schema.md), [mib-library-v1.md](mib-library-v1.md)
and [outstanding-tasks.md](outstanding-tasks.md) for current workflows and limits.

## Historical implementation notes

These notes preserve earlier design stages. Claims about Discovery, Qwt, legacy
Modules controls, temporary libsmi lookup, or unfinished UI describe those stages,
not the current application. The checkpoint above and Redesign Plan supersede them.

## Governing decision: Model C accepted with amendments

The governing MIB architecture is:

```text
MIB files -> Library -> Profile -> Effective Plan -> Environment -> Tree/SNMP presentation
```

Phase 1 implements `MibEffectivePlan` as the single authority for Profile
effective membership and current synchronous Profile runtime construction. The
plan is a pure value: it contains canonical explicit identities, selected
indexed providers and raw SHA-256 values, provider-specific import edges,
missing and ambiguous findings, deterministic dependency-first initial order,
and a schema/policy-versioned hash. It contains no libsmi, widget, model-index,
Tree, `Wanted`, or `mibpreloads` state.

Profiles persist explicit membership only. Automatic membership comes from all
declared identities recursively discovered in the product directory; Custom
membership comes from the user's saved identities. Dependency closure is always
calculated from the selected indexed provider's own imports. The former
catalog-based `MibProfileResolver` effective-membership path has been retired.
Catalog dependency resolution remains only for download planning and does not
define Profile or runtime membership.

Plan-scoped provider policy in this phase is: an Automatic profile's explicit
identity prefers an indexed provider inside that profile directory; otherwise
the deterministic global precedence applies. Equal raw-content providers are
equivalent, while different content at the applicable precedence is ambiguous.
Custom profiles have no folder affinity. Provider-pin UI is deferred.

Dependency-first ordering is an initial attempt, not a compatibility claim.
Runtime materialization continues through `MibBoundedDependencyLoader` with
bounded fixed-point retry. Absolute-path loading selects planned root files but
does not prevent libsmi from searching recursively for an import that was not
already loaded, so planned-versus-actual provider paths are verified and
mismatches are logged.

Phase 2 now defines and synchronously extracts an immutable, value-owned
`MibEnvironment` from the successfully materialized Plan runtime. It preserves
rich module/node/type/table/notification semantics, deterministic lookup
indexes, structured materialization findings, and memory telemetry, and remains
valid after libsmi teardown. See `ADR-001-mib-library-profiles-environment.md`.
Phase-4 value consumers and Phase-5 parser operations have not migrated.
Asynchronous switching, the dedicated libsmi engine thread, Environment
LRU/persistence, and legacy preload migration remain later phases.

Phase 3 migrates the read-only semantic consumers. The Tree and its OID Info
presentation are Environment projections; graph labels, TrapPresenter, and
table classification/planning resolve immutable Environment records. A small
process-wide registry owns the currently published `shared_ptr<const
MibEnvironment>` for stateless graph/trap callbacks, while Tree nodes retain
their own shared ownership. Absence of a Plan-backed Environment produces an
empty Tree or numeric-only labels rather than falling back to libsmi.
There is no built-in **All MIBs** Profile. The built-in default is
**Standards / MIB-II**, and a user who wants every MIB creates an ordinary Custom
Profile. Every Profile, including the default, is materialized normally,
including at startup. Environment replacement invalidates Qt indexes, retains only numeric
OID selection intent, clears OID Info during reset, and restores selection only
when that OID exists in the destination model.

`MibSelection` and Agent value decoding remain Phase 4. Parser loading,
diagnostics, editor validation, and the older MibService parser snapshot helper
remain Phase 5. No engine thread, asynchronous Environment construction, or
Environment cache exists yet.
Persisted Environment caching is deferred. Raw hashes remain authoritative;
normalized semantic hashes are not used.

Patched libsmi remains the authoritative parser, loader, semantic database,
and diagnostic source. Its tolerant handling and historical diagnostic output
are compatibility requirements; application adapters must not reinterpret or
filter parser output.

## Lifetime and serialization

libsmi is process-global and is not treated as thread-safe. Initialization,
module loading, path changes, diagnostic-handler changes, and shutdown remain
serialized on the application thread. A legitimate Preferences refresh may
save the current path, call `smiExit()` / `smiInit()`, restore the path and
handler, reload configured preloads, then rebuild snapshots and projections.
Incremental preferred-MIB loading does not perform that global reset and does
not modify preload settings.

`MibNode` widgets retain a canonical numeric OID rather than an `SmiNode *`.
Module presentation retains `MibModuleRecord`, and `MibViewLoader` retains
canonical module names rather than `SmiModule *` arrays. Any libsmi pointer is
therefore confined to a synchronous lookup or snapshot traversal and cannot
survive a reload.

## Partial Environments and the problem report

An Environment that loses some modules is **Partial and published**, not refused.
`runtimeAuthorityComplete` is about integrity only: the configuration matches the plan,
the loaded files are exactly the authorized files, and at least one requested root
loaded. A failed module, an unresolved import, a listed module the Library cannot
supply, a missing root file, or a stale pin makes the Environment Partial and adds a
problem. The Environment is Unusable only when integrity fails or nothing loaded.

Each build produces a list of `MibProblem` records (`mibproblem.h`, built by
`mibproblemreport.h`) from three sources: the plan's resolution findings, libsmi's own
diagnostics captured during the load (with line numbers, mapped back to the user's
original files), and the load outcomes. Severities: Error (something the user chose
could not be loaded), Warning (loaded, but some definitions may be missing), Note
(informational: modified files, standards issues, parser warnings). Only errors and
warnings count as problems. The report is stored on the immutable Environment.

When several different files declare a module, the first by path is used and a
Duplicate providers warning names every candidate. A changed root file loads as it is
on disk and carries a Modified note; saving from the editor refreshes the recorded hash.

## Value records and services

`MibModuleRecord` identifies a module by libsmi's canonical module name and
captures only presentation metadata. `MibTreeNodeRecord` retains canonical
numeric OID identity and the metadata used by the active browser. Neither
contains libsmi or widget pointers.

`MibService` is the widget-independent boundary for configured search paths,
explicit/preload loading, per-operation results, loaded-module inventory, and
tree snapshots. It does not persist settings. Existing Preferences remains
the sole owner of `mibpaths`, `mibpreloads`, and `misc/automaticloading`.
Directory order and the existing nonrecursive path scan are unchanged.

`MibLoadResult` distinguishes newly loaded, already loaded, and unavailable
modules. Warnings do not change a successful libsmi load into a failure. A
mixed request is partial only when at least one requested module is unavailable.

## Diagnostics

`MibDiagnosticCollector` temporarily installs a libsmi error callback for one
serialized load operation and records callback fields in arrival order. It
retains severity, tag, source path, line, requested module, exact message, and
operation identity. No column is invented because the callback supplies none.
Callers must restore their owning callback after capture; libsmi exposes no
API for reading the previously installed callback.

`MibDiagnosticModel` is a presentation-ready table model for severity,
module/file, line, and original diagnostic wording. The legacy editor list is
retained for this incremental milestone; it still uses error level 9 and the
existing severity colors and wording.

## Tree model, selection, and filtering

`MibTreeModel` owns a value snapshot and exposes display, numeric OID, module,
node kind, access, status, type, and searchable-text roles. Selection identity
is the numeric OID and can be restored after reload if that OID remains.
`MibTreeFilterModel` uses Qt's recursive filtering so matching descendants keep
their ancestors visible. Existing `QTreeWidget` browser behavior remains as a
compatibility projection, but it no longer owns durable libsmi pointers.

Snapshots copy descriptions because current details presentation needs them.
They are rebuilt only after load/reload, not during filtering or selection.
This avoids repeated libsmi traversal at interaction time, at the cost of one
description copy per visible node.

Table queries use libsmi node kinds rather than object names. Selecting either
a TABLE container or its ROW/entry enables **Query Table**; a TABLE selection
is resolved to its ROW definition before the existing asynchronous table runner
derives and queries the columns. COLUMN selections keep their normal object and
instance operations and are not treated as full-table queries by default.

## Compatibility gates and limitations

The unchanged smilint golden regressions for SNMPv2-MIB, IF-MIB, MAU-MIB,
RMON2-MIB, and BRIDGE-MIB remain authoritative. Parser sources and golden files
must not be regenerated or updated as part of application modernization.

Query, table, Discovery, TrapPresenter, and the editor may continue transient
OID-based libsmi lookup. The disabled Qwt graph path remains untouched. The
main-window MIB tree has not yet been visually redesigned, and editor navigation
from a diagnostic remains future work.

Normal startup must not semantically compile every configured MIB/PIB file.
It reads the persistent dependency index, performs size/mtime candidate
inspection, and loads only requested runtime modules through the indexed
identity-to-provider resolver. Full hashing, declaration refresh, dependency
closure verification, and bounded retry remain explicit **Check Dependencies**
work. A missing index produces a stale/needs-checking state, never a legacy
compile-all fallback.

## User-facing responsibilities

The legacy Modules screen is now the top-level **MIB Library**. It preserves its
Available MIB modules, Loaded MIB modules, and runtime module-information views
while owning collection-wide dependency validation. Its explicit **Check
Dependencies** action updates the shared persisted graph and refreshes the
Inventory and Profile projections; merely opening the Library never starts
that work.

**MIB Profiles** are inexpensive selections over identities already known to
the Library. Their effective membership combines explicit selections, the
optional Standards/MIB-II base, and dependency closure from the shared graph.
Editing or switching a Profile does not rescan or revalidate the collection.

**Loaded MIB modules**, within MIB Library, reports actual process-local libsmi
runtime state. It is distinct from the identity-centric Inventory and is not
reduced when a Profile hides modules from the browser.

Runtime preload requests are stored internally as declared module identities.
Legacy filename-based `mibpreloads` remain readable: each candidate filename is
projected through the dependency index to every declaration provided by that
physical file, then deduplicated with identity-based entries. Selecting one
Available file explicitly requests all its declarations. Removing a Loaded row
removes that explicit identity and reconstructs libsmi from the remaining
explicit requests, so shared dependencies remain when still required.

The explicit request set is intent, while the Loaded table is always rebuilt
from actual libsmi module enumeration. Available physical candidates are hidden
only when at least one declaration from their provider file is actually loaded.
Arrow operations reconstruct and verify the requested runtime transactionally;
failed attempts reconstruct the prior known-good loaded identity set and do not
persist the attempted request change.

Library dependency checks reuse the persisted profile signature and per-provider
`verified` state when candidate hashes and index generation are unchanged. A
no-change check performs metadata inspection and projection refresh only. New or
changed provider records lose their verification state, while unchanged records
remain reusable; runtime reconstruction occurs only after semantic work actually
loaded temporary providers.

Structured application diagnostics use local ISO-8601 timestamps with
milliseconds and an explicit numeric UTC offset. Raw libsmi callbacks and
SNMP++ messages now use the same redacted file/UI delivery (2026-11-07), with a
prefix on each physical line. Original library text, including any embedded
library timestamp, is retained inside the wrapped message. Parser diagnostics
and severity generation are unchanged.

## User MIB collection and product profiles

Actual working MIB/PIB sources live below the configurable **MIB Library
Location**. Its cross-platform default is Qt's `DocumentsLocation` plus
`MIB Navigator/MIBs`:

```text
MIBs/
  Standards/           global standards library; never automatic profiles
  Unassigned/          global miscellaneous/download library; never automatic profiles
  <Vendor>/<Product>/  recursive product source; one Automatic profile
```

The packaged `mibs` and `pibs` directories remain pristine initialization and
recovery sources. Initialization copies missing files into Standards but never
overwrites different content. Legacy managed downloads are copied into Unassigned
once, originals are retained, identical files are skipped, and same-name
different-content files are reported and preserved. Changing the configured
root initializes the new location and never deletes or moves the previous one.

Generated state remains under the established application-data locations:
download provenance, the IANA catalog cache, Custom/Automatic stable profile IDs,
settings and migration markers stay in AppData; the dependency/provider index
remains at `GenericData/SnmpB/mibs/dependency-index-v1.json`.

Automatic profiles express product applicability. A non-reserved first-level
directory with immediate child directories is a vendor container: every child
is one recursively scanned product profile named `<Vendor> <Product>`, while
deeper folders never create profiles. A non-reserved first-level directory with
no child directories becomes a simple product profile only when it contains a
supported file directly. Empty product children are retained; empty standalone
first-level directories are ignored. `Standards` and `Unassigned`, including all
descendants, never generate automatic profiles.

File declarations, not filenames, define explicit membership; every identity
in a multi-declaration file is included. Automatic membership is read-only and
its stable ID is stored internally, while the directory remains authoritative.
Custom Profiles remain editable purpose/subset/combination sets.

Global conflict facts retain the precedence Standards, then Unassigned, then all
product trees as one precedence class. Effective Plans make the runtime choice:
an Automatic profile may select its own explicit product provider before the
global default. Same-identity providers with identical raw SHA-256 content are
equivalent; different content remains distinguishable and unresolved unless a
documented plan policy selects it. Provider-specific imports drive closure, so
standards dependencies need not be copied into each product directory. Profile
selection materializes the same plan shown by Profiles and does not rewrite the
legacy `Wanted`/`mibpreloads` preference.

Library and Profile models are reconciled during construction and explicit
collection-changing actions such as Refresh, downloads, root changes, and
profile edits. Merely making the top-level MIBs workspace visible reuses those
models: it performs no collection scan, inventory rebuild, dependency check,
profile regeneration, or libsmi reconstruction. Timings for explicit Library
refresh phases, runtime reconstruction phases, and cached tab activation are
written to the structured diagnostic log.

During transition from the development prototype, `Library/Standards` and
`Library/Imported` are copied safely into the new reserved trees, without
overwrites or deletion. Existing `Profiles/<name>` directories remain in place
and are recognized as legacy Automatic profiles. A directory rename is treated
as removal of the old Automatic profile and discovery of a new one.

## Real-collection fixes (2026-10-31)

Loading a real vendor folder (Extreme Fabric Engine, 98 files) exposed three problems, each now
covered by a tolerance scenario in `snmpb.tolerance-scenarios`:

- **Two selected files declaring one module** (two revisions of `INET-ADDRESS-MIB` side by side)
  stopped the whole profile with "Multiple exact providers declare ...". The resolver now keeps the
  first file by path, sets the other aside for that module, and reports a "Duplicate providers"
  warning naming both (`duplicate-selected-files`).
- **A profile made from one vendor folder could not see the standard MIBs** its files import
  (`SNMPv2-MIB`, `IF-MIB`, `ENTITY-MIB`, ...), so most of the profile failed. Every profile now
  gets the Library's `Standards` folder as the last place to look for an import
  (`MibResolverInputForProfile`, one function used by both the start-up and the normal path;
  `standards-fallback`).
- **A parser crash**: a `TEXTUAL-CONVENTION` whose `SYNTAX` is a type the library does not define
  (here `TimeTicks`, absent from a bad `SNMPv2-SMI` copy) dereferenced NULL in the generated
  parser. The module is now kept with an unknown base type and the problem is reported
  (`unknown-type-in-textual-convention`; removing the fix makes this scenario crash). The fix is in
  `libsmi/lib/parser-smi.c` and mirrored in `parser-smi.y`; the generated file is patched by hand,
  as for the earlier JETDIRECT3-MIB fix, and not regenerated.
