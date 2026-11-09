# ADR-001: MIB Library, Profiles, Effective Plans, and Environments

Status: **Accepted with amendments (revision 3)**

The governing data flow is:

```text
Library -> Profile -> MibEffectivePlan -> MibEnvironment -> consumers
```

The Library owns discovered providers and dependency facts. A Profile owns
explicit applicability. `MibEffectivePlan` is the sole authority for intended
effective identities, provider choices, provider-specific imports, and load
order. `MibEnvironment` is the immutable, value-owned semantic result of
materializing that exact Plan with patched libsmi. Consumers are projections of
the Environment; libsmi is a serialized construction engine, not application
state.

## Implemented state

Phase 1 is complete: Profile presentation and synchronous runtime construction
consume the same `MibEffectivePlan`; legacy `Wanted`/`mibpreloads` do not
participate in Plan resolution.

Phase 2 is complete subject to its deterministic validation gates: the
schema-versioned `MibEnvironment` owns module, type, node, hierarchy, table,
notification, materialization-finding, index, and telemetry values. The
synchronous extractor runs after Plan materialization and before publication as
`shared_ptr<const MibEnvironment>`. Public Environment headers contain no
libsmi or UI types, and lifetime tests query the Environment after `smiExit()`.

Phase 3 is complete: Tree and OID Info projections, graph label resolution,
trap presentation, and read-only table classification/planning consume the
published immutable Environment. These paths have targeted source guards and
post-`smiExit()` consumer tests. `MibTreeNodeRecord` remains only as a temporary
Qt model projection built from Environment values; it is no longer independently
extracted from libsmi. Numeric OID is the stable Tree selection identity.
Before a Profile-backed Plan has materialized, legacy `Wanted`/`mibpreloads`
startup publishes no Environment; migrated consumers remain empty or numeric
and never fall back to live libsmi. A materialization with at least one
successful planned module publishes an Environment with `Partial` status and
failed-identity findings. A materialization with no successful planned module
is fatal and cannot replace the previous published Environment.

> **Amended 2026-10-30:** the built-in `All MIBs` Profile no longer exists. See
> `redesign-plan.md`. Loading every MIB is almost never wanted, and users who
> want it create an ordinary Custom Profile. The text below is the original
> decision and is kept for history.

`All MIBs` is the built-in `builtin.all` Profile, not legacy preload state or
a visibility-only mode. Its Plan deterministically contains every resolvable
Library identity and follows the same materialization/publication path as
Automatic and Custom Profiles. Extractor input from tolerant patched libsmi is
validated at the ownership boundary: a malformed parser node whose declared
OID length has no backing OID storage is recorded and omitted rather than
dereferenced.

Phase 4 consumers retain `shared_ptr<const MibEnvironment>` at request dispatch.
Phase 5 parser operations remain serialized by the single `MibEngine` boundary.

Phase 6 is complete: normal Profile selection computes its Effective Plan on the
GUI thread and queues materialization/extraction to the dedicated MibEngine
worker. The previously published Environment remains active until the current
generation completes successfully. Stale generations cannot publish; pending
requests coalesce to the latest selection. Completed immutable Environments are
held in a 128 MiB in-memory byte-bounded LRU whose identity includes the Plan,
selected provider hashes, Plan policy/schema, Environment schema/builder, and
patched-libsmi engine policy. Cache hits bypass parser reconstruction. The cache
is not persisted. Provider-pin UI and legacy preload migration remain future
work; no synthetic legacy Plan is created.

Phase 7 is complete subject to its deterministic validation gates. Provider
selection remains a pure, per-Plan decision made before parser materialization.
The precedence is: a valid explicit path-and-raw-SHA-256 pin; product-subtree
affinity for an Automatic Profile's explicit member only; the Library's stable
Standards, Unassigned, then product-location precedence; otherwise an unresolved
differing-content conflict. Custom Profiles never receive folder affinity.
Identical raw-hash providers are benign alternatives. The selected physical
provider's imports drive a dependency/provider fixed point bounded at eight
passes; cycles may converge normally, while a pass-limit breach is a structured
incomplete-Plan finding. The converged provider, raw hash, imports, alternatives,
selection reason, requested pin (including an invalid pin), and convergence state
participate in the schema-2/policy-2 Plan identity. Profile storage schema 3 can
retain provider pins, although broad pin-editing UI remains future work.

Phase 8 retires legacy `Wanted`/`mibpreloads` as live authority. On startup a
version-1 migration reads the preserved historical QSettings array exactly once,
normalizes filenames and identities through the Library provider index, and
persists a Custom Profile named `Imported Legacy MIBs` with stable ID
`migration.legacy-mibpreloads.v1`. Missing entries remain explicit intent.
Profile persistence is verified before the durable migration marker is written;
the stable ID makes a marker-write retry idempotent. Empty legacy state creates
no Profile or marker. A valid saved Profile selection is retained; otherwise a
newly imported Profile becomes the initial selection, while later deletion falls
back through the normal built-in policy (now `Standards / MIB-II`) and does not resurrect it.
Legacy keys are retained for downgrade evidence but normal Preferences, module
editing, refresh, parser configuration, and automatic OID loading no longer
write or reconstruct runtime from them. Environment construction continues only
through Profile -> Effective Plan -> asynchronous MibEngine -> Environment.

## Environment schema overview

- Identity: Plan SHA-256, schema/builder versions (currently schema 2 / builder
  2), patched-libsmi identity,
  status, planned/materialized/failed counts, and structured findings.
- Modules: ASN.1 identity (never filename), language, planned and actual path,
  provider hash, symbol-level imports, revisions/LAST-UPDATED, descriptive
  clauses, root name/OID, and module findings. Multiple identities sharing a
  path remain separate records.
- Types: stable qualified IDs, exact base type, declaration, TC parent/ancestry,
  display hint, units, descriptions, typed defaults, typed ranges/sizes, enums,
  and BITS. Signed and unsigned 64-bit values remain distinct.
- Nodes: numeric OID components/text, qualified and unqualified identity,
  kind, parent/ordered children, access/status, syntax/type/TC facts,
  constraints, named values, defaults, row-create semantics, parser-global
  structural ancestors, and descriptive clauses.
- Tables: table/row/column links, ordered columns and index objects, index type,
  `IMPLIED`, `AUGMENTS`, and each index object's type/base type.
- Notifications: ordered OBJECTS OIDs plus SMIv1 enterprise/generic/specific
  facts when represented by a TRAP-TYPE.
- Indexes: deterministic module, qualified-name, unqualified-name candidate,
  numeric-OID alias, type, parent/child, and root lookup. Ambiguous aliases are
  retained rather than silently discarded.

The Environment does not rerun dependency closure, provider precedence, or
profile membership. Provider hashes are retained in module records even though
the Plan hash already covers them; this is intentional defense-in-depth for a
future cache-key validator.

## Current consumer inventory and migration sequence

| Area | Live metadata read today | Environment representation | Future phase |
|---|---|---|---|
| MibViewLoader / BasicMibView / MibNode / MibTreeModel | module filter, OID hierarchy, kind, access/status, syntax, TC, range, enum, descriptions | nodes, types, parent/children, module index | 3 |
| OID Info | qualified identity, OID, module, type/TC, constraints, named values, descriptions | node/type lookup | 3 |
| GraphLabelResolver | longest live OID match, module/name label | numeric OID aliases and qualified names | 3 |
| TrapPresenter/history | notification label/module, varbind object type/TC, enums/BITS/display hints, unknown numeric fallback | notification objects and node/type indexes; absent lookup preserves numeric fallback | 3 |
| Table validation/traversal | TABLE/ROW/COLUMN kind, table-to-row, ordered columns | table/row/column links and ordered column OIDs | 3 |
| MibSelection | retained node/type, writable access, exact SNMP base type, TC ancestry, display hint, ranges/sizes, enums/BITS | access plus complete node/type values | 4 |
| Agent Get/GetNext/GetBulk/walk | numeric-to-symbolic lookup, suffix rendering, effective TC/base type, enum/BITS/value rendering | OID/name indexes and node/type values | 4 |
| Agent Set/table query | writable syntax, signed/unsigned bounds, index order/types, IMPLIED/AUGMENTS, columns | typed constraints and complete table metadata | 4 |
| MibModule / MibService loader | parser init/reset/path/load/enumeration | remains serialized construction operation | 5 |
| MibDiagnosticCollector / MibEditor validation | parser callbacks, flags, validation load | remains engine operation | 5 |

`MibSelection` currently retains `SmiNode*` and `SmiType*` for dialog lifetime;
that is the highest-risk Phase 4 migration. No semantic gap was found in the
Phase-2 schema: all base types handled by Agent/MibSelection, TC ancestry,
display hints, access, typed 64-bit bounds, ranges/sizes, enum/BITS mappings,
defaults, and object identity are retained. Actual formatting policy remains a
Phase-4 consumer concern.

Agent re-resolves nodes for Get, GetNext, GetBulk, walk, Set, table results,
symbolic OIDs, and rendered values. The schema contains each semantic input,
but byte-for-byte replacement of `smiRenderValue()` belongs to Phase 4 and will
require parity tests for its formatting policy.

Table selection remains unchanged in Phase 2. The Environment can reproduce
TABLE and ROW query eligibility, row discovery, ordered columns, declared
indexes, IMPLIED, AUGMENTS, and index type decoding. Column selections remain
ordinary object operations.

Trap presentation remains unchanged. Known notification/object metadata and
ordered varbind definitions are retained; an unknown Environment OID naturally
has no lookup result and can continue to render numerically.
