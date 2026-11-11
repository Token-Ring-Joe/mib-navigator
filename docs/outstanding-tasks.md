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
   **Owner decision 2026-11-11: use the free options only.** Ship unsigned builds (no
   paid Windows or Apple signing, so SmartScreen and Gatekeeper warnings stay and are
   documented), plain archives from CI (zip on Windows and macOS, tar.gz on Linux), free
   hosting through GitHub Releases, and Qt under its free (LGPL) terms with dynamic
   linking and the notices already in NOTICE.md. Revisit signing only if the warnings
   become a real obstacle. Still to do: a release workflow that attaches the archives to
   a GitHub Release when a tag is pushed (the tag and the Release need owner
   instruction), the naming/trademark check, and version labels.

## Planned features and refinements

5. **Query Macros v1 — milestone 2 authorized 2026-11-08.** Definitions,
   storage and offline editing are done ([milestone 1](query-macros-milestone-1.md)).
   Captured scalar/exact runs, grouped Results, Stop and logging are the active
   workstream ([milestone 2](query-macros-milestone-2.md)). Table retrieval is
   implemented ([milestone 3](query-macros-milestone-3.md)); legacy dialog
   retirement remains milestone 4. The four working
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
13. **Credential protection at rest.** Implemented 2026-11-10 as an opt-in master
    password ([design](credential-protection-design.md)). Default is unchanged
    (secrets in the credential files); Credentials > "Password protection…" turns
    the encrypted vault on, off, changes the password, or unlocks. Still open:
    manual acceptance on the Linux desktop (startup prompt, cancel-and-unlock),
    and a later choice of OS-keychain or password-manager backends, which the
    `CredentialSecretStore` interface allows without further changes to the
    repositories. Known limits: the old plaintext may remain in free disk space or
    backups after protecting, and a legacy `usm_users.conf` is left untouched.
18a. **Window width on a real Windows desktop.** CI (2026-11-10) is green on all
    three platforms, but the GitHub Windows runner renders text much wider than
    Linux or macOS, so the smoke layout checks scale their 1024-px window and
    640-px Credentials limits by the measured font width (`SmokeFontWidthFactor`
    in `main.cpp`). Confirm on a real Windows machine with the normal system font
    that the window still shrinks to 1024 px. `SNMPB_SMOKE_FONT_SCALE` emulates
    wide fonts locally; `SNMPB_DUMP_MINWIDTH` lists the widest widgets.
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
    *Measured 2026-11-09 on a copy of the owner's Extreme collection (read-only):*
    61 modules have differing raw copies; **none** differ only by line endings, so
    an EOL-equivalence rule would change nothing there. Of the 38 differing modules
    whose newest copies share a LAST-UPDATED date (so the newest-revision rule cannot
    decide), 18 are identical once comments and whitespace are ignored and 20 differ
    in real content; 16 more modules are decided by the newest-revision rule and 7
    have an undated copy. Recommendation: keep raw SHA-256 for identity and pins; if
    those 18 become a nuisance, add a *comment/whitespace-insensitive* equivalence used
    only to classify conflicts (never for identity), computed with a proper SMI
    tokenizer that keeps quoted strings verbatim. No migration would be needed.
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
19. **Open a problem's MIB from the Problems view (requested 2026-11-11).** The
    Problems description shows the file name and line, but opening it means copying the
    name, switching to the Editor tab, opening the file and pasting. Add a direct way
    (for example double-click or a context-menu action) that opens that file in the
    Editor at the reported line. **Priority: medium** (owner, 2026-11-11). The
    search results already have "Open in Editor at this definition"; reuse that path.
    Not started.
20. **Line numbers in the Editor (requested 2026-11-11).** Show a line-number gutter
    in the MIB Editor, which also makes the line references in Problems usable. Check
    whether the existing `markerwidget` gutter can carry them. **Priority: high**
    (owner, 2026-11-11). **Implemented 2026-11-11:** the existing marker gutter now
    also shows line numbers (right-aligned, width grows with the line count, theme
    colours, numbers beside the first row of a wrapped line). Test `snmpb.markerwidget`.
    **Owner confirmed working 2026-11-11.**
21. **Results readability and layout (requested 2026-11-11). Priority: high**
    (owner, 2026-11-11; applies to all three parts). Three related changes, none
    started. **Status 2026-11-11:** Module column, colorized Value and CSV export in
    display order are implemented and **owner confirmed** ("I see module and I like
    the blue", "Copy as CSV works"). The Results/Log swap is **dropped**: the owner
    likes the current layout (2026-11-11).
    - *Module column:* split `MIB::object.instance` so the flat Results table shows
      Module | Object.Instance | ... with the rest following. Add the column; keep
      the existing columns, including the hidden metadata columns other features use.
      Make it hideable through the existing column-visibility setting. Objects the
      profile does not know leave Module blank. Table results have their own
      column layout and are handled separately. Owner decision (2026-11-11): the
      end column is the **received type** (what the device returned). Syntax is not
      needed in Results because the right-hand pane already shows it; hide the Syntax
      column by default (keep it available through column visibility). Check whether a
      bare wire-type difference (for example Counter32 returned for a declared
      Gauge32) already raises a Warning and add it if it does not.
    - *Colorized Value column:* make the Value stand out, using the same color the
      Log uses (owner, 2026-11-11: the "value" in the bottom Log box is a light
      blue; the two must match). The appearance code maps the original blue to the
      theme's Link color, so this is probably that color; confirm in the Log code. Must follow the Light/Dark/Follow System themes through the shared
      appearance colors (`appearancecolors.h`), keep contrast in both themes, and
      not collide with the existing warning highlight on a value mismatch. Check
      which Log color is meant before building.
    - *View menu choice to swap Results and Log:* a View-menu option, remembered
      between sessions, that exchanges the positions of Results (now under the
      Tree) and Log (bottom panel). Record the layout change in
      [the redesign plan](redesign-plan.md). A wider pane suits the extra Module
      column. **Dropped by the owner (2026-11-11).** The analysis stays for the record:
      Results is now a tab beside Watch
      and Macros in the Query workspace, and Log lives in the shared bottom panel with
      Output and Problems. A swap would move whole tabs between those areas and touch
      the workbench shell, focus routing and the activity badges, so it is larger
      than a menu toggle. Decide first whether the aim is to move Results to the
      bottom panel, or only to make the Results pane wider.
22. **Search field also shows the selected OID (requested 2026-11-11). Priority:
    high** (owner). Give the "search MIBs by name or OID" field above the Tree a
    second use: each time the user clicks an object in the Tree, the field shows that
    object's current OID. Design to settle before work: a query the user has typed
    must not be overwritten by a selection (for example show the OID only when the
    field is empty or not focused, or use a separate read-only line), the OID must be
    copyable, and typing or pasting an OID must still search and jump.
    **Implemented 2026-11-11** (`selectedoidfield.*`, test `snmpb.selectedoidfield`):
    the field shows the selected object's OID unless the user is typing in it,
    clicking in selects the shown OID so typing replaces it, and Enter still searches
    or jumps. Awaiting owner review.

23. **New application icon (2026-11-11).** Replaced the old SnmpB art with an
    original compass-in-a-ring icon chosen by the owner (`app/images/snmpb.svg`,
    `.png` 256 px, multi-size `.ico`, `.icns`). Windows uses the `.ico` through
    `snmpb.rc`; the main window uses the `.png`. Not done: `app/CMakeLists.txt` sets
    no `MACOSX_BUNDLE_ICON_FILE`, so the macOS bundle does not use `snmpb.icns`
    today; and the Linux desktop entry (`Icon=snmpb`) has no installed icon file
    named `snmpb`. A simplified 16 px variant was considered and not requested.
    **Update 2026-11-11:** `app/CMakeLists.txt` now copies `snmpb.icns` into the
    macOS bundle and sets `MACOSX_BUNDLE_ICON_FILE`; this is untested locally and
    needs the macOS CI build and a look on a Mac. The Linux package is a portable
    archive that installs no desktop entry, so there is nothing to wire there; the
    in-app window icon comes from `snmpb.png`.

24. **Bottom panel View menu and Help link (owner feedback 2026-11-11).** The View
    menu had a separate entry for each bottom tab (Output, Log, Problems) plus a panel
    toggle and the Operations button. It now has one toggle, "Show/Hide Operations
    panel" (Ctrl+J), the same as the Operations button; tabs are chosen by clicking
    them. Help gained "Getting Started Guide" (F1), which opens the online guide at
    `docs/getting-started.md` on the GitHub `main` branch; the guide is also packaged
    as `getting-started.md`. The per-tab commands remain as unlisted actions so
    automated checks can open a page. Awaiting owner review.

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
- **Candidate features from the 2026-11-11 competitive review** (none approved
  or started; all must stay within the lab/MIB-engineering scope, not NMS
  features such as alerting, dashboards, inventory or scheduled polling):
  - *Copy as command:* copy the equivalent `snmpget`/`snmpwalk`/`snmpset` line
    for an object or table using the current target, credential and OID. Two
    levels (owner design, 2026-11-11):
    1. Secret-free command. The app knows the credential's security level
       (noAuthNoPriv, authNoPriv, authPriv) and writes the matching syntax
       (`-l`, `-u`, `-a`, `-x`) but leaves out `-A`/`-X`, so net-snmp prompts for
       the passphrases (it prompts only at a terminal, and not when
       `defAuthPassphrase`/`defPrivPassphrase` are set in `snmp.conf`). Passphrases
       on a command line show in shell history and `ps`, so including them is an
       explicit option only. v1/v2c: net-snmp does not prompt for the community,
       so show a placeholder unless the user opts to include it.
    Plan: do level 1 first; level 2 is deferred.
    2. Optional later: a localized key generated for the current target
       (needs its engine ID, which is read from the device; tied to that engine
       and to the auth/priv protocols; never stored, to stay consistent with
       the no-saved-devices model).
    Companion ideas, not decided: a generated `snmp.conf` snippet with the
    non-secret `def*` settings; `mibdirs`/`mibs` for the active profile (default
    to numeric OIDs, since net-snmp's parser is stricter than the patched
    libsmi); import credentials from an existing `snmp.conf` into the vault.
    Never modify the user's `~/.snmp/snmp.conf`.
  - *Semantic MIB diff:* compare two versions of a module by object (added,
    removed, changed syntax/status/access/enums/index), building on the Library
    text Compare. **Priority: medium** (owner, 2026-11-11).
  - *Notification tester:* build a trap or inform from a `NOTIFICATION-TYPE`,
    fill varbinds from the MIB and send it to a chosen target (see the trap/inform
    sender idea below). **Priority: low; possible future feature** (owner,
    2026-11-11).
  - *Headless command line:* for example a collection check and a walk with
    JSON/CSV output and exit codes, so the tolerant parser can lint vendor MIB
    drops in CI. **Status: deferred, below low priority; kept on record, not
    planned** (owner, 2026-11-11).
  - *Export:* tree or module to CSV/JSON/HTML, and walk and table results to CSV
    (table CSV already exists). **Priority: medium** (owner, 2026-11-11). Results
    exports must not include credentials. Owner request (2026-11-11): the user
    chooses which columns to export (for example only Object and Value, without
    Module and Type); consider reusing the Results column-visibility setting.
    Owner request (2026-11-11): from a Query Macro run, export **every result group
    (objects and values plus each table) into one file**. Results show one group per
    tab; the owner finds that fine but wants one export of all of them. Options to
    decide: one CSV with a titled section per group (groups have different columns, so
    sections are separated by a blank line and a title row), a JSON file with one
    entry per group, or a spreadsheet (.xlsx would need a library). The per-group
    column choice above should apply to each group.
  - *Walk-backed simulator:* answer GET/GETNEXT/GETBULK from a saved walk; needs a
    decision because saved-walk import is currently out of scope. **Status:
    completely deferred** (owner, 2026-11-11). If ever built, it would likely be a
    separate application (for example alongside GNS3), not part of MIB Navigator;
    the owner is unlikely to implement it.
  - *SNMPv3 failure explainer:* map timeouts and `usmStats` counters to plain
    causes (engine ID, authentication, time window, unknown user); extends the
    existing refusal explanations. **Priority: high** (owner, 2026-11-11).
    **Partly implemented 2026-11-11:** REPORT-based explanations already existed; added
    a v3-specific timeout message (target, user, security level, likely causes, honest
    about what cannot be known) for Get/Get Next/Get Bulk/Set, for a Walk that never
    gets an answer, and for Detect; and the snmpUnknownContexts /
    snmpUnavailableContexts reports. Table and instance-list timeouts still use their
    short messages. Say
    what was observed and what could not be known; do not guess when an agent
    sends no report. Test with a scripted transport.
  - *Missing-import message (rescoped by the owner, 2026-11-11):* diagnose only; do
    not fetch, add or change anything. For an unresolved IMPORTS entry, say which
    module is missing and why, and suggest what to do. If the collection has a file
    declaring that module but the profile excludes it: "Missing module `<name>`:
    not included in this profile. Consider adding `<path>`." If no file in the
    collection declares it: "Missing module `<name>`: no file in the collection
    declares it." **Already implemented** in the Problems report
    (`mibproblemreport.h`, "Missing dependency": unresolved says the module "is not in
    your MIB Library"; out-of-scope lists the file paths and says to add the folder or
    file to the profile). No change made 2026-11-11. Uses the existing dependency index (declared module names), with
    no extra parsing. No network access and no automatic repair. **Priority: high**
    (owner, 2026-11-11). First compare with the wording the Library and Problems
    view already show, and change only what is missing.
  - *OID calculator:* **closed; already implemented** (owner, 2026-11-11). Typing a
    name such as `ifTable` in the search above the tree lists every match, and the
    context menu has Show in MIB tree, Open in Editor at this definition, and Copy
    OID. The follow-up is item 22.
- **Library flag for bootstrap/definition files (2026-11-11):** a vendor file such
  as Extreme's `def.my` (module `START-MIB`) redefines standard roots and
  textual conventions and produces a parse error ("registers object identifier
  already registered by `org`"). Owner suggestion (2026-11-11): keep this simple
  and show it only as an entry in the Problems tab at the bottom, for example a
  note that the file redefines standard roots and nothing imports it, not a new
  Library screen or an exclusion mechanism. Do not hard-code vendor names.
  **Priority: high** (owner, 2026-11-11).

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
