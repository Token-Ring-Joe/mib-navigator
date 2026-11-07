# Traps raw values, MIB checks, and profile-safe history

Traps remains a top-level workspace. Selecting a notification shows each binding's
interpreted value beside its original received type and raw value, plus units and
plain-language warnings for type, enumeration, range and size mismatches. Warnings
have an icon, palette-aware tint and raw-data tooltip. This is value inspection,
not a byte-level PDU trace.

New traps no longer rebuild the history list or steal the current selection. History
is bounded by the existing preference. If the selected record is evicted, stale
details clear; a different trap is never silently selected. Clear History clears
both the service and views. Copy source IP uses the transport sender for v2c/v3,
and prefers a differing nonzero SNMPv1 agent-address for relayed notifications.
SNMPv3 details show the USM user and security level rather than an empty community.

## Ownership and consistency

`TrapService`/`TrapHistoryStore` remain the only owners of received history.
`TrapVarbind` now retains a deep-copying SNMP++ `Vb`; there are no parallel stored
printable, syntax or OID fields to get out of sync. Embedded NUL/high-bit bytes,
unsigned Counter64 precision and wire types survive input PDU destruction.

`TrapPresenter` uses the existing `ResolveMibObject`, `RenderMibValue`,
`RenderMibOid` and `CheckMibValue`. The separate trap-specific printable-text
parser and formatter retired. Each detailed presentation retains one immutable
active Environment for the entire operation. It never touches libsmi, stores
parser pointers or loads extra modules. The Library/provider index/Profile
selection remains authoritative for creating that Environment. A dependency
check or parser reconstruction cannot invalidate retained trap values or metadata.

`TrapView` handles the existing widgets independently of `Snmpb`, enabling the
same production widget workflows in an offscreen component test. Record IDs
identify selection, not row positions or retained indexes. Incremental receive
updates append/evict rows; profile publication reinterprets existing rows and the
selected details. A failed profile build leaves the last published Environment
in use. Widget/service references are guarded, and signal connections have a
QObject context. Preferences now publishes saved history-limit changes to that same service;
there is no second limit maintained by the view. No persistent settings format,
MIB source, parser behavior, transport or notification security context changed.

## Defects found and fixed

- The old trap formatter reconstructed bytes from printable hex text and could
  not share the value mismatch checker. Typed retention and the shared renderer
  remove that fragile second representation.
- Each incoming notification cleared/rebuilt history, dropping selection/details.
  Incremental rows and stable record IDs preserve selection and clear stale
  details only on eviction/clear.
- Profile changes left notification labels, details and binding interpretation
  stale until reselected. Successful Environment publication now refreshes them.
- Shared value rendering returned blank NULL/exception text. Explicit syntax
  names now match the raw checker, with regression tests with/without enum metadata.
- Review caught and prevented an expansion-preference regression: the new view
  reads the live preference when presenting a trap.
- Saving the trap history limit previously updated settings without updating the
  running service. A Preferences signal now applies the new limit immediately,
  covered by the application Save workflow.
- The full regression exposed a narrow latency margin in synchronous Library/
  Profile widget publication. Painting and automatic column sizing now batch
  around each model publication; common file/folder icons are resolved once,
  and geometry is published before restoring scroll. A 360-file tree regression
  verifies stable selection, scroll and restored update/column modes.
- An added unknown-BITS test initially used bit 7, which the bundled MIB actually
  defines. The test fixture now uses genuinely undefined bit 15. This was a test
  defect, not a product defect or timing flake.

## Validation

- Focused/related tests: **15/15 passed**, including the application's real
  Preferences Save workflow and startup/restart.
- Library/Profile ASAN widget test: **1/1 passed with leak detection enabled**,
  including a 360-file tree refresh preserving stable file selection, scroll,
  painting and column-sizing modes.
- Widget workflow: **47 assertions passed** in Debug and AddressSanitizer,
  including conflicting profile definitions and parser shutdown.
- Complete deterministic Debug suite: **65/65 passed (946.59 seconds)**,
  including **27/27 application/tolerance scenarios** and **5/5 libsmi goldens**.
- Sanitizer components: **3/3 passed with leak detection enabled and explicit
  fixture-parser exclusions** in `/tmp/traps-lsan-parser.supp` (smilex, smiparse,
  smiInitData, smiGetModulePath, loadModule). Trap values/widgets stay checked.
  Unfiltered strict checks report existing libsmi setup leaks: pipeline **1,324
  bytes/55 allocations**, widget **1,596 bytes/66 allocations**. A standalone
  parser-only program reproduces the pipeline's exact count. The initial exclusion
  list missed direct module-loader root nodes (240 bytes); tracing those stacks
  completed the explicit parser exclusions. Neither filtered component success nor
  the full-application run is a leak-free application claim.
- Final application ASAN startup/restart and synthetic trap/Save assertions passed
  with `detect_leaks=0:halt_on_error=1`, retaining the existing full-app legacy leak
  limitation documented in `usm-credential-management-v2.md`.
- Release build/install, installed startup/restart, release-tree verification,
  executable byte comparison, all **355 installed MIB/PIB byte comparisons**,
  and `ldd` (**no missing libraries**) passed. All **401 repository MIB/PIB tree
  files** are unchanged. Source/test hashes remained fixed during final validation.
- `git diff --check` and LF checks passed. The first full run passed 64 tests
  and 26 scenarios but exceeded the existing
  1.5-second dependency UI limit twice (1,504/1,582 ms). An unchanged isolated
  rerun passed at 623 ms. Publication traces showed synchronous Library rows and
  Profile trees consuming the margin. Batched painting/column sizing and shared
  file icons reduce that work; the timing limit is unchanged. The final complete
  run passed without failures or timeouts. This was treated as a performance
  weakness, not dismissed as a timing flake. Final dependency UI maxima were
  **993 ms in Debug** and **872 ms in installed Release**, with the **1,500 ms**
  budget unchanged. Existing Qt API deprecations, unchecked `setuid`,
  obsolete UI tab stops and offscreen platform capability messages remain visible.
- **22 files** changed for this batch; **30 files total** remain uncommitted when
  combined with the prior accepted MIB milestone (five shared files). No scope
  guardrail was triggered and there was no unexpected file expansion.

Synthetic PDU workflows cover SNMPv1/v2c/v3 metadata; exact binary bytes including
NUL; Counter64 maximum; enum, MAC and OID formatting; wrong types/enum/range/size;
unknown BITS and malformed bindings; literal device/MIB markup; missing profiles;
two profiles defining the same OIDs differently; switching back after parser
shutdown; stable selection through incoming traps and eviction; limit changes;
Clear History; the actual clipboard action; live expansion preference; a burst
of 1,100 traps against a 1,000-record limit; and callbacks after widget destruction.
The application's startup workflow also sends a synthetic PDU through production
`Trap::Receive` and verifies the actual Traps widgets, then clears it. No packet is
sent for these assertions.

## Remaining limits and manual validation

History remains in memory only and has a record-count limit rather than a byte
budget. Interpretation intentionally follows the current MIB Profile, including
historical traps. No cancellation/progress or raw packet decoder is added.
DISPLAY-HINT/TC rules beyond the shared renderer/checker's existing coverage
remain outside this batch. The existing SNMPv3 same-USM-user variant notification
rules are unchanged: first saved variant per username supplies the unsolicited
notification default; a successful query can install an exact variant for that
remote engine ID. No physical switch testing or desktop GUI launch was performed.

Windows/macOS need GitHub Actions verification; no push was made to trigger CI.
No commits, pushes or generated `manual-validation/` files were made. Existing
uncommitted MIB fixes and personal untracked files were preserved.

Latest successful Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`.
Release build: `/home/synoptics/src/snmpb-next/build-linux-release`.
ASAN build: `/home/synoptics/src/snmpb-next/build-linux-asan`.
Install: `/tmp/mib-navigator-manual-validation/traps`.
Executable: `/tmp/mib-navigator-manual-validation/traps/bin/mib-navigator`.
Installed dependency check: **passed; no missing libraries**. Ready for desktop UX
validation; Windows/macOS still require CI.

Manual UX check: launch that executable, select a received trap, and confirm raw
and interpreted values are readable. Leave it selected while receiving another
trap and changing the MIB Profile; confirm selection stays and interpretation
updates. Right-click a trap and use Copy source IP. Real-device receipt/security
acceptance remains the owner's check if desired.
