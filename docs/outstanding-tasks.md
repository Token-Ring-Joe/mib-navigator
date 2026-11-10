# Outstanding work after owner approval — 2026-11-07

The owner approved the accumulated work and authorized a local commit on
2026-11-07. This closes the finishing pass's owner-approval checkpoint; it does
not claim a new desktop test run, native-platform validation or real-device test.
The authoritative build order remains [redesign-plan.md](redesign-plan.md).
Completed/superseded entries in historical documents are not reopened here.
The later owner request to do five autonomous items authorizes the follow-up
marked implemented below; its latest evidence is [here](autonomous-five-validation.md).
The owner subsequently authorized the five maintenance/release items. Their
current evidence/status is [maintenance-five-validation.md](maintenance-five-validation.md).
After reviewing that status, the owner authorized Query Macros milestone 1.
See [its checkpoint and working notes](query-macros-milestone-1.md). This inventory
does not authorize a commit, push, tag or publication.

## Release work still outstanding

1. **Current cross-platform CI and packages (step 20).** After an authorized
   push, build/test/install/package/extract/verify this checkpoint on Linux,
   Windows and macOS. Current Linux Debug/ASAN/installed Release checks pass;
   that does not establish current Windows/macOS results. The CI configuration
   uses Qt 6.10.3 while the VM uses 6.11.2; review that version difference and
   the existing headless-launch-test exclusion when preparing the release.
2. **Native desktop acceptance.** Check Windows/macOS deployment, themes/Follow
   System, fonts, scaling, keyboard/menu behavior and platform integration on
   the actual packaged builds. Owner approval of the Linux work is recorded;
   automation did not launch a desktop window.
3. **Controlled real-device acceptance.** Complete any remaining RC lab matrix
   in [manual-real-device-acceptance.md](manual-real-device-acceptance.md),
   particularly SNMPv3 authentication/privacy/refusal and incoming notification
   behavior. Earlier table acceptance is historical evidence, not a new test of
   every current path. Requires separate device/test authorization; no scanning.
4. **Distribution preparation.** Confirm actual Qt/dependency inventories,
   notices and the chosen Qt distribution route; review release-owner naming/
   trademark responsibilities, translations, version/RC labeling and native
   artifacts. Decide whether signing/notarization is required (currently absent).
   Tagging, publishing and pushes to `origin` or `nas` require owner instruction.

## Planned features and refinements

5. **Query Macros v1 — milestone 2 authorized 2026-11-08.** Definitions,
   storage and offline editing are done ([milestone 1](query-macros-milestone-1.md)).
   Captured scalar/exact runs, grouped Results, Stop and logging are the active
   workstream ([milestone 2](query-macros-milestone-2.md)). Table retrieval and
   legacy dialog retirement remain milestones 3 and 4. The four working
   milestones are definition model/repository/editor; captured scalar/exact
   runs and grouped Results; table groups with all/prompt/fixed instances; then
   regressions, old Multiple Varbinds retirement and release validation.
   Estimate: **6–10 focused development days**, plus owner review/platform CI.
   See [the agreed design](query-macros-v1-design.md). Single-object SET stays.
6. **Global SNMPv3 context name — implemented 2026-11-07.** Preferences,
   persistence and captured UTF-8 request semantics are present. Per-query context
   is only a later possibility; see [follow-up validation](autonomous-five-validation.md).
7. **Watch persistence and interval editing — implemented 2026-11-07.**
   Definitions save without secrets/results, return paused after restart, and
   support selected-row interval edits. Port/context remain captured.
8. **Watch columns/tables.** Settle the all-instances/table design before work:
   discovery, polling, membership changes, persistence and presentation remain
   undecided. This is separate from Query Macros and retains captured endpoints.
9. **Broader MIB value checks — implemented in the maintenance batch.** Shared
   integer/octet hints, ASCII/UTF-8 warnings, qualified DisplayString/DateAndTime/
   RowStatus semantics, Counter64 constraints and undefined BITS are covered.
   Unsupported/zero-length/wide-numeric hint forms retain received presentation;
   arbitrary vendor DESCRIPTION semantics are not inferred. Further TC-specific
   rules remain incremental refinements. Tolerant MIB loading is unchanged.
10. **MIB Problems tools — implemented 2026-11-07.** Text/severity filtering,
    sorting and snapshot-based CSV copy/export are present.
11. **Logging refinements.** Decide whether/how Watch polls belong in the
    request log. Raw libsmi/SNMP++ diagnostic timestamp wrapping, redaction and
    shared file/UI delivery are implemented (2026-11-07). Full PDU/packet decoding
    is optional later, not an
    unimplemented requirement of the current request log.

## Maintenance and known limitations requiring separate work

12. **Residual parser leaks.** Diagnostic-message and application/UI ownership
    leaks were reduced by about 84% in the same headless workflow (92,179 to
    14,567 bytes). Unsuppressed residual stacks are historical scanner strings and
    pending nodes. Their compatibility-sensitive cleanup remains separate work;
    parser/scanner sources were not regenerated. Qt deprecations and known
    unchecked results were corrected, including fail-closed privilege dropping.
    The clean Release build still reports SNMP++ address/TimeTicks formatting
    warnings and a const-qualification warning in the generated libsmi parser.
    Valid protocol boundary checks pass under strict ASAN; the legacy LP64
    TimeTicks type-width assumption remains a separate vendor hardening concern.
    SNMP++ also renders the extreme unsigned IPv6 scope ID as signed in
    printable text. These warnings are retained, not suppressed. Exact final warning and sanitizer
    results are in the maintenance record.
    Broad ASAN still disables leak detection; isolated strict checks retain it.
13. **Credential protection at rest.** Decide and implement an OS-keychain or
    other protection strategy; current local secret storage is reversibly
    encoded, not encrypted. Preserve safe migration and current credential IDs.
14. **Same-username SNMPv3 notification variants.** Decide how to improve the
    unsolicited-notification limitation: the first saved variant supplies the
    default until a successful query teaches a device's engine-specific keys.
    Credential security edits clear learned mappings. See
    [USM credential management](usm-credential-management-v2.md).
15. **Parser shutdown/cancellation.** Verification is asynchronous, but shutdown
    may wait for a running uninterruptible parse. Stronger cancellation/bounds
    require a separate compatibility-conscious design; no parser replacement is
    implied.
16. **MIB line-ending identity policy.** Decide whether CRLF/LF-only differences
    should count as equivalent providers and how old pins/profiles migrate.
    Raw SHA-256 is currently authoritative. Never normalize user/source MIBs as
    a side effect of this decision.
17. **Translations and remaining distribution review.** Current security,
    dependency, Library and architecture guidance has been reconciled; retired
    device/Graphs/Discovery/transfer designs and earlier credential stages are
    clearly marked historical. Translation completion/native-language review
    remains outstanding. Current redesign/settings/release guides win.

18. **Library conflict explanations — resolved 2026-11-09.** The Library now judges
    conflicts for the viewed profile, lists the competing files with revisions on the
    Providers tab, offers a read-only Compare and a Use This Copy action, and applies the
    owner's newest-revision rule: a strictly newest copy resolves the conflict
    (reported as "Resolved conflict"); ties and undated copies stay "Conflict". See
    the MIBs page rework section in [the redesign plan](redesign-plan.md). Not done: an
    Editor-based side-by-side merge (the owner has not decided whether it is wanted).

## Optional or conditional decisions, not scheduled commitments

- **Compiled/persisted MIB data and Environment cache policy (step 15):** pursue
  persistence/LRU work only if measured startup/search/memory behavior needs it.
- **Implementation check (step 19):** compare a live device with its selected
  profile; optional and separately authorized.
- **Additional UI decisions:** a general compact-density setting; moving Log
  to a top-level workspace; a provider-pin editor (the resolver already supports
  pins); and removal of unused legacy tree/dialog code after a call-path audit.
- **Trap history extensions:** history is in memory and record-bounded, not
  persisted or byte-budgeted. Additional retention controls need a product
  decision; current retained traps intentionally follow the active MIB Profile.
- **Catalog enhancements:** automatic update comparison needs reliable revision
  metadata; current IANA data does not support it. Additional RFC/other catalog
  extraction/mappings are separate from the working manual RFC extractor.

## Historical ideas awaiting triage

[todo.txt](../todo.txt) preserves the old SnmpB backlog. Unverified old reports
are not confirmed current bugs. Remaining ideas needing a decision include OID/
source bookmarks, multiple Editor documents, table mirroring, a trap/inform sender,
packet/session capture, Editor SMI dialect/severity controls, a smaller privileged
trap-receiver helper, character-encoding cleanup and reviewing other forks.
Historical macOS editFinished/multiple-varbind crash reports and incomplete
UCD-SNMP-MIB loading require current reproduction before scheduling a fix.

Do not reopen completed CMake/out-of-tree/CI groundwork, table index decoding,
trap clearing, source navigation, busy/Stop behavior or credential Name/Version
columns. Graphs, Discovery, saved devices and offline saved-walk browsing are
retired/out of scope, not outstanding tasks.

## Earlier approved checkpoint

[Finishing validation](finish-review-validation.md): 65/65 deterministic tests,
all 27 tolerance scenarios, five libsmi goldens, focused 7/7, ASAN 8/8 and strict
widget leak checks 5/5 passed. Release install/dependency and installed workflows
passed. Source hashes match that run; this approval/inventory update changes
only documentation. Rebuilding after the commit will update About's build revision;
the existing installed artifact correctly retains its pre-commit build identity.
