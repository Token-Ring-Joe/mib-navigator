# Maintenance and release-readiness batch — 2026-11-07

Owner scope: compiler warning cleanup; measured application leak reduction;
broader MIB value checks; Linux archive validation; documentation reconciliation.
Query Macros remains deferred until the owner reviews status after these five.
Prior approved/uncommitted work and personal files are preserved. No commit,
push, publication, desktop GUI or physical-device SNMP is included.

## Implemented and reviewed

- Deprecated QMessageBox, QLibraryInfo and UTC date/time APIs use supported Qt
  forms. Fixture opens are checked. A failed Unix privilege drop now stops startup
  rather than continuing privileged; normal unprivileged startup remains unchanged.
- Formatted libsmi diagnostic buffers are freed after synchronous borrowed-message
  callbacks. Initialization releases its empty sentinel and stops retaining a stack
  parser. Real parser pending trees retain their documented compatibility policy.
  Logging/Trap controllers have QObject owners, the Varbinds UI wrapper is freed,
  and About uses a scoped dialog. Failed SNMP-session fallbacks release the
  previous session. The existing disabled-transport failure guard is regression-tested; fatal startup now preserves user
  settings/parser files instead of deleting them. A real headless process fixture
  checks the error and unchanged bytes. Parser/scanner sources are not regenerated.
- One shared hint formatter supplies integer scaling/bases and octet grouping,
  repeats, separators, ASCII and UTF-8. Unsupported forms fall back to received
  presentation. Qualified standard TC ancestry supplies DisplayString NVT,
  DateAndTime calendar/time/offset and RowStatus read-state warnings. Counter64
  constraints retain all 64 bits; undefined BITS warn. SET range/SIZE alternatives
  now accept any permitted alternative, and successful validation clears old errors.
- Release-tree checks reject canonical SNMPv3 credential JSON, case variants,
  private profiles/indexes/Watches and diagnostic logs. A deterministic inert-tree
  policy test covers clean and forbidden fixtures. Actual Linux package extraction,
  runtime inventories and installed workflows are validated separately below.
- Current security, dependency, Library and architecture guidance is reconciled.
  Retired designs and earlier credential phases are marked historical. Linux
  archives explicitly require a compatible system runtime.

## Ownership, compatibility and limits

Library/index/Profile/effective-plan/Environment ownership is unchanged. All new
value behavior consumes immutable copied metadata; it neither loads extra MIBs nor
changes files, provider hashes, Profile membership or parser acceptance. Query,
tables, Watch and Traps continue sharing rendering/checking. No new model indexes
or parser pointers survive resets. Standard TC rules require qualified ancestry,
so vendor types with a matching short name do not silently acquire another type's
rules. Hint parsing/output are bounded; unknown hints retain received values.

DISPLAY-HINT is presentation guidance, not a fixed packet-length requirement.
Short final groups remain supported; trailing incomplete UTF-8 at a hint boundary
is discarded per [RFC 2579 section 3.1](https://www.rfc-editor.org/rfc/rfc2579.html#section-3.1).
Zero-length octet specifications, numeric groups over eight octets and excessive
hint metadata retain raw presentation. Arbitrary TC DESCRIPTION semantics are not
inferred. Existing range/SIZE metadata remains authoritative.

Identical unsuppressed application startup/workflow measurements reduced leaks
from **92,179 bytes / 1,888 allocations** to **14,567 bytes / 632 allocations**
(about **84% fewer leaked bytes**). No application/UI-owned allocation roots remain in that measured
report. Remaining stacks are historical scanner strings and parser pending nodes;
source comments explicitly warn that eagerly freeing pending trees broke useful
MIB behavior. They were not casually re-enabled. This is leak reduction, not a
leak-free full application. A dedicated strict ASAN diagnostic test covers 20,000
callbacks without suppressions. An initial 80-byte initialization sentinel leak
exposed by that test was traced and fixed.

The wider sanitizer rebuild exposed additional warnings in `mibtreemodel.cpp`
and `mibcollectionservice_test.cpp`. The prepared fixes use scoped row-filter
changes (supported by the minimum Qt 6.10) and checked fixture writes. The owner
explicitly authorized a **51-file exception** to the 50-file guardrail for these
two files. Existing 49-file validation was interrupted and preserved before
restarting against the final source; this is not a timing flake.

Review also exposed trailing hint punctuation when a final UTF-8 fragment is
legitimately discarded or repeated groups consume no display text. Punctuation is
now held until actual text follows; regressions cover discarded UTF-8, zero repeats
and interior terminators. This fix stays within the approved 51 files. Validation
before that final review is retained separately and replaced by a complete run.

## Validation status

Final source validation passed:

- Focused Debug checks: **22/22**.
- Optimized Release focused checks: **10/10**.
- Complete permitted deterministic suite: **67/67**, including all **27** tolerance
  scenarios and all **five** unchanged libsmi diagnostic goldens.
- ASAN invalid-access/lifetime workflows: **16/16**
  with leak detection disabled for the documented full-parser residuals.
- Separate strict ASAN checks: **11/11**, leak detection enabled,
  no suppressions; includes the 20,000-callback diagnostic lifetime test.
- Final Debug, ASAN and dedicated Release builds passed. Corrected application
  warnings did not recur. The clean Release build retained **8 vendor warning
  emissions**: SNMP++ address/TimeTicks format bounds and generated libsmi parser
  const qualification. None were suppressed; logs are preserved. Strict ASAN
  boundary checks passed for maximum SNMP32 TimeTicks and IPv4/IPv6/scoped UDP
  addresses. The legacy LP64 TimeTicks width assumption remains separate hardening
  work. SNMP++ also renders an extreme unsigned scope ID as signed (`%-1`); the
  numeric scope storage is unchanged. No third-party parser/scanner regeneration
  or additional file scope is included.
  `git diff --check` passed. Source fingerprints cover **1,948** implementation,
  test and build-policy files; all matched the validated source.
- Release install/tree checks and `ldd` passed. The installed executable matches
  the Release build. All **355** installed MIB/PIB resources match source/build;
  all **401** source MIB/PIB raw hashes remain unchanged.
- The actual install rejected injected canonical credential JSON and a case-variant
  Watch lock, then passed after those task-owned fixtures were removed. Policy
  fixtures also passed Linux/Windows/macOS layout checks; these are not native builds.
- Linux TGZ creation, fresh extraction, all-file byte comparison and extracted
  `ldd` passed. Installed normal/150% and extracted normal offscreen startup/restart
  passed, including safe disabled-transport failure. A scripted loopback GET through
  the extracted application's connection bar passed. No physical device was contacted.
- Actual shared-library hashes/package ownership, toolchain/Qt versions and packaged
  license hashes are in `dependency-license-inventory.json` in the evidence directory.
  The archive requires a compatible system runtime.

The final report is refreshed into the install/archive and the complete extracted
file manifest is rechecked after this documentation-only update; executable and
MIB/PIB bytes must remain identical. The evidence includes stage logs, JSON results,
source fingerprints and the final artifact audit.

Initial fixture compile/generation mistakes were corrected locally and retained as
evidence. The initial strict sanitizer failure exposed the fixed initialization
sentinel leak. The unsuppressed full-application leak measurement still exits nonzero
for the documented parser leaks; it is not counted as a passing strict application
run. Earlier combined runs were deliberately interrupted for the final reviewed
fixes and replaced by this complete run. No unexplained failures, flakes or timeouts
remain in final validation. Qt offscreen emits expected platform-plugin capability
messages (for example `propagateSizeHints`); desktop behavior remains a manual gate.
Cross-platform native CI remains unrun; Linux-only
validation cannot establish native Windows/macOS behavior.

## Artifacts and final status review

- Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`
- ASAN build: `/home/synoptics/src/snmpb-next/build-linux-asan`
- Latest successful Release build: `/home/synoptics/src/snmpb-next/build-linux-maintenance-release`
- Stable verified install: `/home/synoptics/mib-navigator-validation/maintenance-five-2026-11-07`
- Executable: `/home/synoptics/mib-navigator-validation/maintenance-five-2026-11-07/bin/mib-navigator`
- Verified Linux TGZ: `/home/synoptics/mib-navigator-validation/maintenance-five-2026-11-07-evidence/packages/MIB-Navigator-1.0.0-rc1-linux-x86_64.tar.gz`
- Extracted tree: `/home/synoptics/mib-navigator-validation/maintenance-five-2026-11-07-evidence/extracted-package/MIB-Navigator-1.0.0-rc1-linux-x86_64`
- Evidence: `/home/synoptics/mib-navigator-validation/maintenance-five-2026-11-07-evidence`

Current platform/lab/release-owner decisions and deferred work remain in
the source-tree `docs/outstanding-tasks.md`. Query Macros remains deferred pending the owner's status review. Native Windows/macOS CI, desktop UX,
translation review and controlled real-device acceptance remain separate gates.


This milestone is ready for desktop UX validation. Minimum review: launch the
executable above, open/close About, and confirm ordinary MIB browsing looks normal. Headless checks cover behavior and lifetime;
a desktop window was not launched. No new real-device test is required implicitly.
The remaining release gates are native CI/packages, native desktop acceptance,
separately authorized device tests, translations and distribution/signing decisions.
The known parser leaks, credential protection-at-rest decision, parser cancellation,
notification variants and Watch-table design remain tracked separately.

Final task scope is **51 files**, with the explicit two-file owner exception recorded
above. No unexpected file expansion beyond that exception occurred. Inherited work
and personal files remain preserved. No repository `manual-validation/` output was
modified. No commit, push, tag or publication was performed.
