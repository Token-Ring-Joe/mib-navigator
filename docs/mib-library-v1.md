# MIB Library, Profiles and source tools

Current workflow: 2026-11-07. The source-tree `docs/redesign-plan.md` is authoritative.
Older Available/Loaded module arrows and hidden preload preferences are retired.

## Collection and Library

MIB source files live in a configurable user-visible collection, by default
`Documents/MIB Navigator/MIBs`. Its reserved `Standards` and `Unassigned` trees
participate in the global Library. Other Vendor/Product folders generate
Automatic Profiles; vendor names are not hard-coded. Filename and declared
module identity can differ, and one physical file can declare several identities.

The Library owns discovery, providers, raw-content SHA-256 identity, dependencies,
provenance and validation metadata. Refresh discovers physical additions/removals;
Check Dependencies performs serialized semantic checking against Library providers.
It does not make whichever Profile is active the authority for Library coverage.
Background work publishes copied state and restores the latest requested Profile.

Bundled MIB/PIB resources seed missing standard working files. Installation files
are not edited. Existing different-content user files and old managed trees are
preserved during migration. Legacy `mibpaths`/`mibpreloads` settings remain on disk
but do not add hidden provider precedence. Indexes/caches belong in private
application data, outside the source collection. Refresh never normalizes MIB bytes.

## Profiles and the active Environment

Automatic membership follows folder contents and is not edited member-by-member.
Custom Profiles select explicit module identities/providers for a particular task.
The Profiles editor selection is independent of Query's active MIB Profile.
The connection bar selects the actual MIB Environment used by Query.

The effective plan resolves dependencies and authorizes providers. Duplicate
selection respects explicit pins, provider scope, newest valid LAST-UPDATED and
stable path order. Useful imperfect MIBs continue loading; missing or malformed
items produce actionable Problems while independent authorized modules remain
available. No unrelated preloaded parser set controls the displayed Tree.

## Get MIBs from IANA

Open **Get MIBs from IANA…** from the Library. Opening browses cached catalog data
and sends no request. Catalog refresh and downloads are explicit. Downloads capture
their destination so later Profile changes do not retarget them.

The IANA provider uses HTTPS, validates redirects/TLS, bounds transfers and records
local provenance/hash information. Failed refresh preserves the prior cache.
It does not provide reliable comparable revision metadata, so automatic Check for
Updates is not offered. The catalog is not a second source of runtime membership.
Additional RFC-to-module mappings remain deferred; the manual RFC extractor is
available from the MIB menu.

## Editor, Problems and value checks

Editor verification runs asynchronously in a fresh serialized parser with Library
provider metadata; unsaved text wins. Stale document/Library completions are ignored,
and shutdown drains parser work. A running parse may still delay shutdown.
Saving an edited MIB refreshes its recorded hash, Library and active Environment;
editing is normal and does not turn a file into an unauthorized provider.

Problems is available only in MIBs. It shows the published Environment report with
module/file/line, cause, effect and suggested action. Text/severity filters and
sorting affect presentation only. Copy/Export CSV captures the visible report
before a file chooser and includes complete source details. A background update
never navigates to Problems or opens the Operations panel.

Received-value warnings are separate: Results, tables, Watch and Traps use the
same copied metadata and renderer/checker. Unsupported DISPLAY-HINTs fall back to
received presentation; warnings never rewrite a MIB or prevent its useful loading.

See [settings-schema.md](settings-schema.md), [security.md](security.md) and
[maintenance validation](maintenance-five-validation.md) for persistence, security,
verified behavior and remaining limitations.
