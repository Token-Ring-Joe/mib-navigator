# MIB Navigator Development Instructions

## Project Goal

Modernize the existing open-source SnmpB application into MIB Navigator:

- A modern, free, cross-platform graphical SNMP/MIB engineering tool.
- Preserve SnmpB's unusually tolerant handling of real-world and imperfect
  vendor MIBs.
- Preserve SNMPv1, SNMPv2c, and SNMPv3 functionality.
- Modernize the application using Qt 6 Widgets and CMake.
- Maintain Windows, Linux, and macOS portability.
- Provide strong MIB browsing, dependency handling, table operations, value
  watching, traps, and related SNMP engineering tools for lab and MIB
  engineering work. MIB Navigator is not a network management system.

Do not trade away proven SnmpB compatibility merely for cleaner or newer code.

## Current Direction and Build Order

`docs/redesign-plan.md` is the authority for product direction, retired
subsystems, the target layout, and the build order. Read it before starting
feature, UI, or removal work. Where older documents in `docs/` disagree with it,
the redesign plan wins.

Work one build-order step at a time unless the user instructs otherwise.

---

# PRIMARY ENGINEERING MODE

You are not merely a code generator.

Act as:

1. Primary developer
2. Code reviewer
3. Automated QA engineer
4. Architecture reviewer

Your goal is to minimize the amount of manual debugging and validation required
from the user.

Do not stop merely because:

- the code compiles
- one focused test passes
- the complete test suite passes

A green test suite is necessary but not sufficient.

Before handing work to the user, perform every reasonable deterministic,
headless, programmatic, architectural, and consistency check available.

---

# Development Environment

Primary development platform:

- Ubuntu Linux VM, x86_64
- Qt 6 Widgets (system Qt 6 packages)
- GCC
- CMake with Ninja
- VS Code
- GitHub (`origin`), plus a backup bare repository on the Synology NAS
  (`nas`)
- Branch: `modernize/qt6-cmake`

All development happens on the Linux VM.

Working copy location:

- The working copy is a git clone on the VM's local disk:
  `~/src/snmpb-next`. Builds, tests, and git operations run there.
- The former working tree on the NAS share is archival after Redesign Plan
  step 2. It is at `/mnt/nas/Personal/Software/snmpb-next`. Do not develop,
  build, or run git in it, and do not delete it. The user retires it.
- Back up by pushing committed history to the `nas` remote, only when the user
  asks.

NAS share:

- The Synology share `//172.16.20.249/AI-Projects` is mounted at `/mnt/nas`
  by a kernel CIFS entry in `/etc/fstab` (systemd automount: it mounts on first
  access, unmounts after 10 idle minutes, and `nofail` keeps a missing NAS from
  blocking boot). Credentials are in the root-only
  `/etc/samba/nas-synoptics.creds`. Never put credentials in the repository,
  docs, prompts, or command lines.
- The `nas` remote is `/mnt/nas/Personal/Software/git/snmpb-next.git`, a bare
  repository. It is the only NAS location git should touch.
- Use `/mnt/nas`, never the GNOME gvfs path under `/run/user/1001/gvfs/`. The
  gvfs mount is slow, does not support chmod, and some git commands fail there.
  Do not build or run tests on any NAS path.
- If `/mnt/nas` looks empty, run `ls /mnt/nas/Personal` to trigger the
  automount. If it still fails, check `systemctl status mnt-nas.automount` and
  `sudo dmesg | grep -i cifs`.
- The VM is on an isolated AI VLAN that can reach only the NAS and the internet.
  The NAS account is limited to the folder AI may change. No real SNMP devices
  are reachable from the VM, which matches the SNMP / Network Safety rules.

Windows and macOS remain supported targets. They are built and tested by GitHub
Actions CI, not locally. Do not introduce Linux-only behavior; keep platform
differences behind portable Qt or CMake abstractions.

Use the actual configured toolchain from the current CMake/build environment
rather than relying on stale hard-coded compiler minor versions.

Do not unnecessarily introduce compiler-specific behavior that harms
cross-platform portability.

---

# Git / Working Tree Rules

Do NOT automatically:

- commit
- push
- merge
- tag
- publish
- reset
- revert
- stash
- discard user changes

unless explicitly instructed.

Before substantial work:

1. Inspect `git status`.
2. Understand the current worktree.
3. Determine whether uncommitted work belongs to the approved current
   workstream.

Approved uncommitted work MAY be continued.

Do NOT require a clean worktree if the user has intentionally been validating
an uncommitted milestone.

Never silently remove or undo work merely to obtain a clean state.

Keep commits focused around tested milestones, but the user performs commits
unless explicitly instructed otherwise.

## Change-Scope Guardrails

If a task would modify more than 50 files, STOP before making the changes and
report why.

Never modify or track generated deployment output under `manual-validation/`
unless explicitly authorized.

If any single operation expands scope by more than 25 unexpected files, STOP.

One-time exception: the line-ending normalization step in
`docs/redesign-plan.md` (step 2) may change the git-reported state of many
files. It does not count against the 50-file limit, provided that no file
content other than line endings changes and this is verified and reported.
The exception does not cover any other change made in the same task.

## Line Endings

Text files use LF unless `.gitattributes` says otherwise. Preserve each file's
existing line-ending style when editing. Never introduce mixed CRLF/LF line
endings into a file.

Never change the bytes of MIB files (including line endings) as a side effect
of other work. MIB provider identity uses raw-content SHA-256 hashes.

---

# Autonomous Development Loop

For every feature, bug, or architectural change:

1. Inspect the existing implementation first.
2. Trace the real source call paths.
3. Define expected user-visible behavior.
4. Identify relevant persistence/state ownership.
5. Implement the smallest coherent change.
6. Build.
7. Run focused tests.
8. Investigate failures.
9. Fix genuine defects.
10. Rebuild.
11. Rerun focused tests.
12. Run related regressions.
13. Review the complete diff.
14. Look for architectural contradictions.
15. Add regression tests for weaknesses discovered during review.
16. Run the complete permitted deterministic suite.
17. Perform release/install validation.
18. Only then request manual UX validation.

Repeat this loop until the implementation is internally consistent.

Do not hand obvious debugging work back to the user.

---

# Validate USER WORKFLOWS, Not Just Helper Functions

Every feature must have one or more explicit end-to-end workflows.

Example:

    User places product MIBs in the configured MIB collection
        ↓
    Application discovers them
        ↓
    Appropriate Automatic profile appears
        ↓
    User selects the profile
        ↓
    Appropriate MIB environment/tree is available
        ↓
    User browses or queries MIB objects

Tests should exercise as much of the complete workflow as practical.

Do not rely only on isolated unit tests for functionality spanning multiple
components.

---

# Realistic Test Data

Do not test only tiny ideal fixtures.

MIB-related tests should include representative difficult cases where relevant:

- hundreds of MIBs
- filename != declared module identity
- multiple identities in one physical file
- recursive imports
- circular imports
- shared dependencies
- missing dependencies
- identical duplicate providers
- different-content duplicate providers
- malformed vendor MIBs
- loaded and unloaded modules
- stale persisted settings
- repeated refresh
- restart/persistence
- migration from older layouts
- empty profiles/folders
- large Automatic profiles
- profile switching

Prefer deterministic synthetic fixtures unless explicitly authorized to use
real external data.

---

# Architectural Consistency Review

After every meaningful feature, explicitly ask:

    Are two different subsystems maintaining conflicting truths?

For MIB functionality, compare at minimum:

- physical MIB collection
- provider/dependency index
- MIB Library
- Automatic Profiles
- Custom Profiles
- active Tree profile
- libsmi state
- persisted settings
- legacy Module Preferences / Wanted state if still involved

Do not accept a design where the UI shows a modern abstraction while old SnmpB
state silently remains authoritative underneath it.

If legacy architecture conflicts with the new architecture:

1. trace it
2. explain why it remains involved
3. determine whether it is still necessary
4. consolidate or retire it where appropriate

Do not create another workaround layer without identifying the ownership issue.

---

# Current MIB Architecture Direction

The intended conceptual responsibilities are:

## MIB Library

The MIB Library owns global MIB knowledge:

- discovered modules/files
- providers
- filenames and paths
- module identity
- provenance/origin
- validation state
- dependency relationships
- unresolved dependencies
- duplicate/conflicting providers
- module/file metadata
- dependency checking
- library refresh

Dependencies are a LIBRARY concern.

Do not make dependency management primarily a Profile concern.

## MIB Profiles

Profiles define MIB applicability/selection.

Two user-facing profile types are intended:

### Automatic Profiles

Generated from product-line MIB folders.

They answer:

    Which MIBs apply to this product line?

Their explicit membership is derived from physical folder contents and declared
MIB identities.

They are not manually editable member-by-member.

### Custom Profiles

Created and edited by the user.

They answer:

    Which MIBs do I want for this particular purpose?

Examples:

- lab
- customer
- troubleshooting subset
- migration combination

Custom profile membership is manually editable.

## Tree

The Tree presents the currently selected MIB environment.

Do not assume that merely filtering a pre-existing unrelated libsmi loaded set
is sufficient.

When modifying profile/tree interaction, explicitly determine which subsystem
is authoritative for the actual MIB environment.

---

# User-Visible MIB Collection Direction

Actual MIB source files should live in a user-visible configurable location,
not only in the application install directory or hidden AppData storage.

Default concept:

    Documents/MIB Navigator/MIBs/

Expected organization:

    MIBs/
        Standards/
        Unassigned/
        <Vendor>/
            <Product>/

Examples:

    MIBs/
        Standards/
            IETF/
            IANA/
            IEEE/

        Extreme Networks/
            Fabric Engine/
            Switch Engine/
            ERS/

        Aruba/
            AOS-CX/

Reserved first-level trees:

    Standards
    Unassigned

These participate in the global MIB Library but do not automatically create
product profiles.

Vendor/Product directories may create Automatic Profiles.

Do not hard-code vendor names.

Internal generated data such as indexes, hashes, caches, settings, and other
bookkeeping belongs in ApplicationLocalData/AppData.

---

# MIB Loading Tolerance and Visibility

Flexibility and visibility are the owner's top priorities for MIB handling:

- **Load everything that can load.** A problem with one MIB, one import, one listed
  module, or one edited file must never stop the rest of a profile from loading. An
  Environment is Partial, not refused, when some modules failed and the loaded files
  are exactly the authorized ones. Only "nothing at all could be loaded" is an error.
- **Say what went wrong and why.** Every skipped or degraded item appears in the
  Environment's problem report (`MibProblem`, `mibproblemreport.h`) with the module,
  file and line, the plain-language cause, what it affects, and what the user can do.
  The Problems tab shows it. The header badge, status bar, and startup state all
  describe the same report.
- **The user decides.** MIB Navigator never edits MIB files. The user can fix a problem
  or carry on knowing which parts may not work.
- **Edited MIBs are normal.** Every MIB file is editable. A root file whose content
  differs from the hash recorded when it was added is loaded as it is on disk and gets
  a small "Modified" note, not a problem. Saving from the editor refreshes the stored
  hash, the Library index, and the active Environment.
- **Duplicates are resolved, not refused.** When several different files declare a
  module, the first by path is used and the report says which, and why.
- **Integrity stays strict.** The application never silently loads a file that the plan
  did not authorize. That is a consistency check, not a reason to refuse a profile.

Any change that makes a profile or Environment fail where it previously loaded with a
report must come with a scenario in `cmake/RunSnmpbToleranceScenarios.cmake`. That
test builds libraries containing every kind of problem, starts the real application
headless, and checks both that it loads and that it explains what it could not.

---

# libsmi Rules

The repository contains a patched version of libsmi.

This is critical because MIB Navigator must continue handling real-world and
occasionally imperfect vendor MIBs.

Do NOT casually:

- replace libsmi
- upgrade libsmi
- regenerate parser/scanner sources
- rewrite parser behavior
- tighten parsing simply for standards purity
- change diagnostics behavior without regression testing

Preserving useful compatibility is more important than stylistic cleanup.

Long-lived raw libsmi pointers are dangerous across parser resets.

Prefer stable value objects such as:

- module identity
- OID
- copied metadata

over storing parser-owned pointers across resets/reconstruction.

---

# libsmi Golden Tests

The historical golden diagnostics currently cover:

- SNMPv2-MIB
- IF-MIB
- MAU-MIB
- RMON2-MIB
- BRIDGE-MIB

Run all five after changes affecting MIB parsing, loading, indexing, profiles,
tree construction, or related MIB behavior.

Expected:

    5/5 passed

Do not regenerate expected results merely because implementation output changed.

Investigate why it changed first.

---

# Qt Model / Lifetime Safety

This application has previously encountered stale model/index problems.

When modifying Qt models, views, profile switching, tree rebuilds, or refreshes,
explicitly inspect for:

- stale `QModelIndex`
- stored indexes surviving model reset
- stale pointers to model-owned nodes
- invalid selection restoration
- dangling QObject ownership
- callbacks arriving after model destruction
- asynchronous completion targeting stale UI state

Do not preserve a `QModelIndex` across a model reset unless its lifetime is
provably safe.

Prefer stable identifiers and reacquire indexes after rebuilding models.

---

# Persistence / Migration Safety

For persisted settings or user files:

Never silently:

- delete user data
- overwrite different-content user files
- discard old settings
- rewrite user files unnecessarily
- migrate destructively

Migration should be:

- explicit in behavior
- safe
- idempotent
- testable
- rollback/failure aware

A partially failed migration must not leave the application pointing at an
invalid state.

Historical settings may still use older SnmpB organization/application names.
Do not casually rename persistence roots as part of unrelated work.

---

# SNMP / Network Safety

Do not perform SNMP operations against real devices unless explicitly
authorized for that specific acceptance test.

Use:

- scripted transport
- deterministic transport
- synthetic responses

for automated tests.

Do not scan networks or probe real devices merely to validate a build.

---

# GUI Launch Rule

Do NOT launch the GUI unless explicitly instructed.

The user performs normal manual GUI validation.

Headless/unit/integration testing is preferred.

If a GUI smoke test is excluded because launching the GUI is prohibited,
report that clearly.

---

# UI / UX Review

A technically correct implementation can still be incomplete if the UI is
confusing.

Before manual validation, review:

- terminology
- disabled controls
- duplicated screens
- hidden functionality
- unclear state
- stale legacy concepts
- whether the workflow matches the user model

Ask:

- Would a network engineer understand what to do without reading source code?
- Is a read-only view being represented as a disabled editor?
- Are two screens exposing the same information?
- Are internal implementation concepts leaking into normal UI?
- Does the UI clearly show what changed after an import/refresh/profile action?

Do not preserve obsolete UI simply because it existed in SnmpB.

---

# Legacy SnmpB Preservation Rule

Preserve legacy BEHAVIOR where it contributes compatibility or useful SNMP/MIB
functionality.

Do not automatically preserve legacy UI architecture.

Examples requiring explicit review when touched:

- Module Preferences
- Available/Loaded MIB arrows
- old preload settings
- long-lived parser state assumptions
- widget-owned application data

If a legacy mechanism conflicts with MIB Navigator's newer architecture, trace
and resolve the conflict rather than silently layering another system on top.

---

# Connection Model

The address book, Device Manager, saved Agent Profile devices, Discovery, and
Graphs are retired. See `docs/redesign-plan.md`.

The connection model is three independent controls at the top of the main
window:

- Target: typed IP address or hostname with a recent-history list
- Credential: saved, reusable credentials managed on a separate screen
- MIB Profile: the MIB environment used to interpret results

Port, timeout, retries, and similar transport settings are global settings in
the menu, not main-window controls.

Do not create or reintroduce saved-device, address-book, folder, or Discovery
subsystems.

Existing `agents.conf` Agent Profile data is not migrated. Leave it on disk
untouched. Do not delete it.

Continue evolving the existing reusable credential architecture.

Operations that run over time (such as Watch) capture their target and
Credential reference when created. Changing the connection bar must never
retarget them.

---

# UI Technology

Do not rewrite the application into QML at this stage.

Qt Widgets remains the preferred UI architecture.

Separate application/core logic from widget ownership where practical.

Use QML only after a deliberate future architectural decision.

---

# Third-Party Dependency Architecture

Third-party components should remain independent CMake targets where practical.

Conceptually:

    MIB Navigator
        |
        +-- Qt 6
        +-- application/core
        +-- patched libsmi
        +-- SNMP++
        +-- crypto/platform dependencies

The vendored Qwt library is being removed together with Graphs. Do not add new
uses of it.

Do not casually fold third-party implementation source directly back into the
application target.

---

# Build / Test Rules

After meaningful backend work:

1. Build affected targets.
2. Run focused tests.
3. Run related regressions.
4. Run complete permitted deterministic suite.
5. Run five libsmi goldens when MIB-related.
6. Run `git diff --check`.
7. Perform Release build.
8. Install to a manual-validation directory with `cmake --install` and verify
   the install tree, including that `ldd` reports no missing libraries for the
   executable. Do not launch the GUI to verify it.
9. When a change could affect Windows or macOS, say so and rely on GitHub
   Actions CI for those platforms. Do not push to trigger CI unless the user
   asks.

Do not silently suppress compiler warnings.

Record warnings separately from failures.

Warnings involving pointer size, ownership, DLL linkage, or 32/64-bit
assumptions should remain visible for future review.

---

# Test Failure Policy

When a test fails:

1. Investigate it.
2. Classify it:
   - genuine product defect
   - test defect
   - timing flake
   - environment/tool limitation
3. Fix genuine defects.
4. Rerun focused test.
5. Rerun related regressions.
6. Rerun complete permitted deterministic suite.

Do not report a failure to the user when you can reasonably diagnose and fix it
yourself.

A timing failure should not be labeled a flake without repeated evidence.

---

# Adversarial Self-Review

Before requesting manual validation, review the implementation as if trying to
break it.

Inspect:

- complete git diff
- error paths
- rollback paths
- null/empty states
- stale settings
- restart behavior
- refresh behavior
- migration behavior
- model resets
- async lifetime
- parser resets
- duplicate sources of truth
- unexpected legacy dependencies
- performance regressions
- large-data behavior

Add regression tests when the review exposes a plausible failure mode.

---

# Manual Validation

Manual testing should be the final step, not the first debugging step.

Only ask the user to test behavior that cannot reasonably be validated
programmatically.

Manual instructions must be short and non-technical.

Good:

    1. Start this executable.
    2. Select Extreme Networks Fabric Engine.
    3. Confirm the expected tree appears.
    4. Tell me what happened.

Bad:

    Validate provider precedence, dependency closure, QModelIndex lifetime,
    runtime intent, and parser state.

The user should validate USER EXPERIENCE, not internal implementation.

---

# Release / Manual-Validation Build

For every substantial milestone, create one stable manual-validation build.

Always report:

- exact build directory
- exact install directory
- exact full executable path
- whether the install tree dependency check (`ldd`) passed

The user launches manual-validation builds on the Linux VM desktop. Windows
deployment trees and `windeployqt` are not part of local validation.

Do not assume a previously deployed directory is still valid.

---

# Final Report Format

Do not provide a long development diary.

Report:

1. User-facing behavior now implemented.
2. Important architecture discovered or changed.
3. Bugs discovered during autonomous validation.
4. Bugs fixed.
5. Remaining known limitations.
6. Focused test results.
7. Complete deterministic suite result.
8. Five libsmi golden result when applicable.
9. `git diff --check` result.
10. Release build/install result.
11. Any warnings, flakes, timeouts, or environmental limitations.
12. Whether the implementation is ready for manual validation.
13. The minimum manual validation still required.
14. Exact latest successful build and install directories.
15. Exact full executable path.
16. Whether the install tree dependency check passed, and whether Windows or
    macOS need CI verification.
17. Whether any file-scope safety guardrail was triggered, including the number of files modified and any unexpected scope expansion.

If something appears architecturally wrong even though all tests pass, say so.

Do not declare a feature complete merely because the test suite is green.
