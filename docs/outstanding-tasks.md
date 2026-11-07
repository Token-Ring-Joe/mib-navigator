# Outstanding work after owner approval — 2026-11-07

The owner approved the accumulated work and authorized a local commit on
2026-11-07. This closes the finishing pass's owner-approval checkpoint; it does
not claim a new desktop test run, native-platform validation or real-device test.
The authoritative build order remains [redesign-plan.md](redesign-plan.md).
Completed/superseded entries in historical documents are not reopened here.
No implementation, push, tag or publication is authorized by this inventory.

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

5. **Query Macros v1 — highest-priority deferred feature.** Design and estimate
   agreed on 2026-11-07; implementation remains deferred. The four working
   milestones are definition model/repository/editor; captured scalar/exact
   runs and grouped Results; table groups with all/prompt/fixed instances; then
   regressions, old Multiple Varbinds retirement and release validation.
   Estimate: **6–10 focused development days**, plus owner review/platform CI.
   See [the agreed design](query-macros-v1-design.md). Single-object SET stays.
6. **Global SNMPv3 context name.** Add the planned Preferences/request setting
   with captured operation semantics. Current connection-bar requests always
   use the default empty context. Per-query context is only a later possibility.
7. **Watch persistence and interval editing.** Save/reload Watch definitions
   safely and allow per-row polling-interval edits. These are not implemented.
8. **Watch columns/tables.** Settle the all-instances/table design before work:
   discovery, polling, membership changes, persistence and presentation remain
   undecided. This is separate from Query Macros and retains captured endpoints.
9. **Broader MIB value checks.** Extend DISPLAY-HINT and TEXTUAL-CONVENTION
   validation beyond the implemented type, range, size, enumeration and BITS
   checks; preserve tolerant parsing and the shared renderer/checker ownership.
10. **MIB Problems tools.** Add further filtering/sorting and copy/export.
11. **Logging refinements.** Decide whether/how Watch polls belong in the
    request log. Normalize raw libsmi/SNMP++ diagnostic timestamps with the
    structured logger. Full PDU/packet decoding is optional later, not an
    unimplemented requirement of the current request log.

## Maintenance and known limitations requiring separate work

12. **Legacy leaks and warnings.** Investigate existing full-application/libsmi
    leaks and older UI ownership paths. Resolve Qt deprecations and unchecked
    returns (including Linux privilege setup and test-file writes) without
    suppressing warnings or casually changing the patched parser. Broad ASAN
    invalid-access passes currently disable leak detection; strict widgets pass.
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
17. **Documentation and translations.** Complete translations and reconcile or
    clearly date older architecture/security/dependency documents where they
    still describe retired saved-device, Graphs, legacy Modules or profile
    transfer behavior. The current redesign plan/settings/release guides win.

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

## Current validation reference

[Finishing validation](finish-review-validation.md): 65/65 deterministic tests,
all 27 tolerance scenarios, five libsmi goldens, focused 7/7, ASAN 8/8 and strict
widget leak checks 5/5 passed. Release install/dependency and installed workflows
passed. Source hashes match that run; this approval/inventory update changes
only documentation. Rebuilding after the commit will update About's build revision;
the existing installed artifact correctly retains its pre-commit build identity.
