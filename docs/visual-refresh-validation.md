# Visual refresh validation

Owner request: 2026-11-04. Implementation and automated validation are finished.
Ready for owner visual/interaction validation.

## User-facing changes

- Query, MIBs and Traps remain at a fixed global navigation height. The top
  Get/Get Next/Get Bulk/Walk/Table/Set/Stop buttons are removed. Operations remain
  in menus and right-click actions; Escape still stops a request.
- Go to OID and collection search belong inside Query. Reparented controls remain
  visible and keep a compact height. Results and Watch remain inside Query.
- Shared palette-aware styling gives controls, tabs, tables and focus states a
  consistent appearance. Native font sizes and explicit numeric/monospace roles
  are preserved; existing styled controls follow application font changes.
- The connection controls use captions above their fields and accessible names.
  Credentials show aligned Name and Version columns with native selection.
- The Inspector separates object identity, OID, metadata and description, with a
  Copy OID action tied to the selected object rather than unsubmitted navigation
  text. Empty selection clears the Inspector and disables copying.
- The Results summary uses a full-width row. Operations indicates verification,
  Library work, profile builds and SNMP activity.
- Library has a separate, nonmodal Get MIBs from IANA window. Opening it uses the
  cached catalog without making network requests. Catalog/download status stays
  visible there after the main Library refreshes.
- Profiles says Edit profile. Inspecting or editing an inactive profile does not
  activate it in Query. The connection bar owns the active profile choice.

## Ownership and safety review

The physical collection, shared Library index, profile service and immutable
runtime Environment retain their existing responsibilities. The IANA table is
only a projection of the shared catalog. Downloads capture their destination
profile when started; switching the connection bar or closing the window cannot
retarget them. A missing destination leaves downloaded files in the Library and
reports that profile membership could not be changed. Root-changing controls
are unavailable while a catalog/download operation runs.

Copy OID stores a copied string, not a parser pointer or model index. Selection
reset handling preserves stable OID restoration and the chooser's ability to
accept manually entered numeric OIDs. Credential identity and secret persistence
formats are unchanged. No settings roots, MIB parser behavior or MIB bytes were
changed. Existing approved uncommitted work is preserved.

Autonomous review fixed hidden controls after reparenting, stretched Query
controls, a narrow wrapping Results summary, stale Inspector contents, mixed
Library/IANA status messages, download destination retargeting, and stylesheet
font snapshots. The filtered tolerance runner also now skips its live-agent
assertion when that scenario was not selected; unfiltered live-agent proof is
unchanged.

## Validation

- Focused changed-workflow run: 15/15 passed, including the five goldens and
  offscreen application startup/restart. Final font regression: 1/1 passed.
- Complete final deterministic Debug suite: 65/65 passed, 1089.02 seconds.
  This includes all 27 tolerance scenarios (933.62 seconds), synthetic SNMP
  query/table/Walk/Stop flows, and close-during-startup checks.
- Five historical libsmi goldens: 5/5 passed in the focused and complete runs.
- ASAN changed workflows: 8/8 passed serially, including startup/restart.
  Full-application/libsmi leak detection was disabled because of known legacy
  leaks; this is not a claim that the complete application is leak-free.
- Strict ASAN leak checks for appearance, credentials, shell, Results and Library
  widgets: 5/5 passed with leak detection enabled.
- Release build, installation and VerifyReleaseTree passed. Installed startup
  and restart passed offscreen at 150% scaling; screenshots were inspected.
- ldd resolved all executable dependencies. The package contains 333 MIBs and
  22 PIBs, all byte-identical to their sources. All 401 source MIB/PIB files
  matched the pre-work hash baseline.
- Final git diff --check passed. All 18 frozen source/test/runner hashes matched,
  changed text files use LF, and all 20 task files were expected.

An initial ASAN startup check timed out while compilers and other heavy tests
ran concurrently. Dependency responsiveness also exceeded its timing budget
under that overlap. That Debug run was stopped; logs were retained and the
checks were rerun serially without changing timing thresholds. The serial ASAN
run passed (8/8, 73.90 seconds), as did strict widget leak checks (5/5,
5.66 seconds). The complete Debug rerun passed as reported above. Its dependency UI gaps were
610–940 ms, below the unchanged 1500 ms budget; action calls returned in 0 ms.

Existing compiler warnings remain visible: deprecated QMessageBox and
QLibraryInfo calls, plus an unchecked setuid return in the existing Linux
privilege setup. Qt offscreen emits its known propagateSizeHints warning.
No desktop GUI was launched and no real-device SNMP or external IANA requests
were made. Windows and macOS need GitHub Actions verification; no push was made.

The old Check for Updates placeholder is removed: the current IANA catalog does
not provide reliable comparable revisions. Automatic revision comparison is
not implemented. Desktop appearance and interaction remain for owner validation.

## Manual validation artifact

- Release build: `/home/synoptics/src/snmpb-next/build-linux-release`
- Debug build: `/home/synoptics/src/snmpb-next/build-linux-debug`
- Sanitizer build: `/home/synoptics/src/snmpb-next/build-linux-asan`
- Install: `/tmp/mib-navigator-manual-validation/visual-refresh`
- Executable: `/tmp/mib-navigator-manual-validation/visual-refresh/bin/mib-navigator`
- Dependency check: passed, no missing libraries.

Minimum owner check: launch the executable, switch Query/MIBs/Traps and confirm
navigation stays put; inspect the revised Credentials, Inspector and IANA
window. This is a visual/interaction review, not an internal debugging task.

This task changes 20 files (18 source/test/runner files and two documents).
No file-scope safety guardrail was triggered and no unexpected scope expansion
occurred. No generated deployment output was tracked. No commit or push made.
