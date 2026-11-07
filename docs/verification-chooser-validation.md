# Editor verification and OID chooser fixes — 2026-11-04

## User-visible behavior

Editor verification resolves dependencies from the full configured MIB Library,
independently of the active Query profile. The opened file's folder is also
available for local sibling imports. Every check parses the current editor text
in a fresh parser session, so prior loaded modules cannot hide an unsaved error
or supply an import which is no longer available. Files with names different
from their declared identities remain supported. Identical duplicate providers
remain usable; conflicting required providers produce explicit diagnostics.

Editor text and dependency copies are staged in private temporary storage, so
verification does not require writing next to the original source. Diagnostic
paths point back to the original files. Actual conformity findings remain:
ACCESSBIND-PIB retains its attribute/group findings without spurious missing
imports; ATM-MIB's RowStatus default remains Warning (4).

The OID chooser uses the existing model-based tree in selection-only mode.
Opening it no longer builds a QTreeWidgetItem and icon for every OID. It keeps
its tree and MIB interpretation fixed for the lifetime of the dialog, even if
another profile is published. Expand/Collapse/Find remain available. Query,
Walk, SET and table-query commands are not exposed in this selection view.
Scalar .0 instances now retain their scalar metadata instead of being treated
as unknown OIDs; genuinely unknown numeric OIDs retain numeric fallback.

## Architecture and ownership

The configured collection and existing Library provider resolver remain the
source of global dependency knowledge. Verification refreshes a value copy of
the published index using update(..., persist=false); it never publishes or
writes that verification-only snapshot. Existing index update callers retain
their default persistence behavior. Declared identities map to byte-verified
provider copies in the temporary search directory. No parser/scanner behavior,
MIB source bytes, settings roots, profile membership or legacy path precedence
is changed.

Parser work stays serialized by MibEngine. Validation starts with an empty
parser, restores flags/search paths after parsing, and removes its temporary
files. The existing asynchronous restoration rebuilds the latest requested
Query profile; the published immutable Environment remains usable meanwhile.
No raw libsmi pointer is retained across resets. The chooser owns a copied
MibTreeModel snapshot alongside its captured immutable Environment, rather than
subscribing to the mutable main tree or the legacy view registry. QModelIndexes
remain within that owned model's lifetime.

## Regression coverage and review findings

Synthetic Library fixtures cover recursive imports, filename/identity mismatch,
repeated verification, removal/recovery of available imports, unsaved edits to
an existing identity, identical duplicates, conflicting duplicates and unchanged
persisted index bytes. Bundled ACCESSBIND-PIB and ATM-MIB cover the two reported
real-world diagnostic cases without modifying those files.

Application startup/restart workflows open a local editor file that imports a
filename-mismatched Library dependency absent from the active MIB-II profile,
then verify and restore the profile. They also open/cancel the real chooser,
check model ownership, scalar instance handling and empty-profile fallback.
The full-library consumer workflow checks model construction/OID selection,
profile publication with an open chooser, and disabled network actions.

Review found and fixed an existing scalar .0 interpretation defect. Early test
iterations corrected fixture parser flags and an assumed syntax-error tag;
libsmi reports that syntax message under its existing internal-other tag.
Review also corrected the workflow assertion for profiles without sysDescr,
where numeric fallback is expected. Parser diagnostics were not altered to make
tests pass.

## Validation

Focused tests: 12/12 passed, including all five libsmi goldens (39.74 seconds).
Focused large chooser construction/selection: 56 ms for 25,704 Environment nodes.
Additional normal/empty-profile application regressions passed after the final
numeric-fallback assertion correction. AddressSanitizer focused workflows: 4/4
passed (69.70 seconds), including the full-library chooser. ASAN application
startup/restart passed. detect_leaks=0:halt_on_error=1 retains the documented
legacy application/libsmi leak limitation; these are invalid-access checks,
not a leak-free claim.

Release build/install, installed startup/restart, release-tree verification and
ldd passed with no missing shared libraries. All 355 installed MIB/PIB files match
source bytes and all 401 repository MIB-tree files match the earlier baseline.
Frozen source hashes, LF checks and git diff --check passed. Existing Qt
QMessageBox/QLibraryInfo/QDateTime deprecations, unchecked setuid/QFile open
warnings, obsolete UI tab-stop entries and offscreen-plugin notices remain
visible. No warning was suppressed. The complete deterministic suite passed: **65/65**, 899.70 seconds,
including all 27 application scenarios and all five libsmi goldens. Final full-run
chooser construction/selection was **57 ms for 25,704 nodes**
(126 ms under AddressSanitizer). The PIB verification regression took 441 ms;
representative malformed/valid editor checks took 229/211 ms.
Implementation is ready for manual UX validation.

Exact artifacts:

- Debug: /home/synoptics/src/snmpb-next/build-linux-debug
- ASAN: /home/synoptics/src/snmpb-next/build-linux-asan
- Release: /home/synoptics/src/snmpb-next/build-linux-release
- Install: /tmp/mib-navigator-manual-validation/verification-chooser
- Executable: /tmp/mib-navigator-manual-validation/verification-chooser/bin/mib-navigator

Validation logs and source-freeze records use /tmp/verification-chooser-*.
The full task diff was reviewed against /tmp/verification-chooser-before;
earlier accepted uncommitted work was preserved. Sixteen task files changed;
the accumulated workstream contains 62 paths plus two untouched personal
untracked paths. No scope guardrail or unexpected expansion was triggered.
No commit or push was made.

## Limits and manual validation

Verification still waits for the serialized parser operation; cold/large checks
can take time. This batch does not add progress or cancellation. Each chooser
copies model value records, so construction remains proportional to the tree
size; it avoids per-OID widget/icon work. Existing legacy application/libsmi
leaks, compiler warnings and native desktop theme limitations remain separate.
Query Macros and the legacy Multiple Varbinds SET-oriented workflow remain
deferred as agreed. Windows/macOS require CI verification.

No desktop GUI or physical-device SNMP acceptance test was performed. Manual UX
validation is limited to verifying ACCESSBIND-PIB with MIB-II selected, then
opening the OID chooser with a large profile and confirming it responds promptly.
