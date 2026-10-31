# MIB Inventory and Profiles V1

The MIB Library workspace separates two concepts:

- **Inventory** describes locally installed and authoritative-catalog modules.
- **MIB Profiles** describe which modules are visible in the working OID tree.

The Inventory table is read-only and deliberately compact. Its Origin labels map packaged
content to **Built-in**, authoritative IANA content to **IANA**, and other locally supplied
content to **Imported**. Download and failure state remains session state presented through
row icons, tooltips, the Info tab, and the status line rather than a permanent table column.

The Info tab snapshots parsed libsmi metadata into value records. Module Information shows
the declared module identity, numeric MODULE-IDENTITY OID, latest parsed revision timestamp,
organization, description, contact information, optional reference, and compact revision
history before the separate File Information/provenance section. Multiline source text is
shown by wrapping plain-text labels without flattening line breaks. Module scanning retains
descriptive clauses instead of using libsmi's `SMI_FLAG_NODESCR` discard option.
The two sections share a horizontal 3:2 stretch-weighted splitter so descriptive module text
receives more width while both panes resize with the window. Built-in file information shows
Origin, catalog revision (or `—`), filename, and local path; provider, URL, download time,
SHA-256, and routine state are hidden when they do not apply. IANA downloads retain all
available provenance fields. State is shown only for transient or exceptional conditions.

Profiles are stored in `mibs/profiles-v1.json` using schema version 5 and atomic `QSaveFile`
writes. A current manifest record contains a stable ID, display name, exact `roots`, ordered
dependency-resolution `scope`, and exceptional provider `pins`. Each root records its source
path, expected SHA-256, and every declared identity. Each scope records a stable collection
identifier plus canonical path metadata. Each pin records the identity, exact provider path,
expected SHA-256, and optional reason. Dependencies are never persisted as editable roots.

Schemas 1â€“4 remain readable solely for migration. Their `members`, `modules`, `directory`,
`includeStandardBase`, `providerPins`, and `unresolvedLegacyModules` fields are compatibility
input, not current runtime authority. Migration persists the schema-5 manifest before any
external completion marker can be written. A current manifest is never converted back to
legacy membership.

`Standards / MIB-II` is the only built-in profile. There is no built-in `All MIBs`:
loading every MIB is rarely useful, and users who want it create an ordinary Custom
profile (an earlier version created an empty `All MIBs` Custom profile on first run;
existing copies are preserved as ordinary Custom profiles). The maintainable V1
standards base is:

    SNMPv2-SMI, SNMPv2-TC, SNMPv2-CONF, SNMPv2-MIB,
    IF-MIB, IP-MIB, TCP-MIB, UDP-MIB, ENTITY-MIB, HOST-RESOURCES-MIB,
    BRIDGE-MIB, Q-BRIDGE-MIB, LLDP-MIB, INET-ADDRESS-MIB

Custom profiles store only intentional exact roots. Activation resolves recursive IMPORTS
within the ordered scope, applies pins before automatic selection, and produces a sealed
Effective Runtime Plan. Missing/changed roots and missing/changed/non-declaring pins are hard
errors with no provider fallback. Catalog additions outside scope are irrelevant. Add Files
adds roots; Add Folder takes a one-time recursive root snapshot and does not attach the folder.

The browser selector requests an Environment built from the resolved Plan, isolated runtime
stage, and fresh parser state. The previous immutable Environment remains available until the
new Plan is verified and publishable. Connection-to-profile association and device support
detection remain deferred.

Legacy folder snapshots are converted once into ordinary manifests. For a legacy profile
with a broad generated Standards population (at least twenty Standards files, non-Standards
content, and unresolved legacy identity residue), non-Standards exact files are conservatively
retained as roots while Standards is kept as resolution scope. For other exact-member profiles,
uniquely reachable dependency-only files become derived; cyclic, ambiguous, and unexplained
files remain roots so user intent is never silently lost. An Unassigned
file is retained when it is part of the non-Standards legacy intent; this is not a universal
rule that all Unassigned files belong to every profile.
