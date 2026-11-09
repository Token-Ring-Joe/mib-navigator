# Profile metadata and portable transfer v1

## Ownership

MIB Navigator keeps three configuration responsibilities separate:

- `agents.conf` contains SNMP endpoint and request configuration.
- `device-tree.conf` contains folder hierarchy, profile placement, and order.
- `profile-metadata.conf` contains optional notes and tags.

Stable `profileId` is authoritative across all three. Renaming a profile does
not migrate metadata. Missing metadata is equivalent to empty notes and tags,
and a missing metadata sidecar is normal.

## profile-metadata.conf

The QSettings/INI sidecar now uses schema version 2; version 1 remains readable:

```ini
[schema]
version=2

[profiles]
size=1
1\profileId=stable-profile-id
1\notes=Free-form user notes
1\tags=Datacenter, Core
1\preferredMibs=IF-MIB, SNMPv2-MIB
```

Records with an empty ID and duplicate records for the same ID are ignored.
Reading does not create or rewrite the sidecar. Metadata is removed when its
profile is deleted. Empty metadata is not retained as a record.

Notes preserve entered text. Tags use comma-separated entry in the current
editor. Whitespace is trimmed, empty tags are discarded, and duplicates are
compared case-insensitively while preserving the spelling of the first tag.
Autocomplete is intentionally deferred.

The profile editor holds profile and metadata working copies. Cancel writes
none of the three configuration files. OK persists the Agent Profile first and
then its metadata. New profiles begin with empty metadata. Duplicate operations
copy notes and tags to the new stable ID; they do not copy or create USM users.

## JSON transfer format

Portable files are UTF-8 JSON. Version 2 adds preferred MIB names while version
1 remains import-compatible:

```json
{
  "format": "snmpb-next-profile-transfer",
  "version": 2,
  "credentialPolicy": "omitted",
  "profiles": [],
  "metadata": [],
  "folders": [],
  "placements": []
}
```

Exports can contain one profile, a folder subtree, or all Device Manager data.
Folder and placement sibling `order` values are retained. Folder-subtree roots
become roots in the exported document so the file is self-contained.

Non-sensitive exported profile fields include display name, address, port,
protocol flags, retries, timeout, GET-BULK values, SNMPv3 security-name
reference and level, and context values. Notes and tags are exported.

SNMPv1/v2c read and write communities are sensitive and are always omitted in
v1. Every profile is marked `credentialsOmitted: true`, and the document-level
policy is `omitted`. SNMPv3 secrets are stored by the separate USM subsystem
and are never exported. `securityName` is only a reference; importing it does
not create a USM user or invent credentials. Imported community fields are
empty and must be configured locally before use. Sensitive export is not
implemented.

## Import validation and conflicts

Import follows parse, validate, plan, and apply stages. Invalid JSON,
unsupported versions, malformed records, duplicate incoming IDs, metadata for
missing profiles, invalid placements, and missing folder parents are fatal.
Fatal validation does not mutate application state.

An incoming profile ID already used locally is remapped to a newly generated
stable ID. Folder-ID collisions are handled the same way. Metadata and
placements are rewritten through those maps. Same display names with distinct
IDs remain distinct; names are never an overwrite key. Placement-ID collisions
are regenerated. No destructive replacement policy exists in v1.

Application writes are staged to three temporary sidecars. Only after every
staged file is valid are the three live files replaced; replacement failure
rolls already replaced files back to their originals. Import then reloads the
profile service, metadata service, and Device Manager model.

## Compatibility and future fields

Neither existing sidecar schema changed, and old installations without
metadata continue to work. Optional MIB associations naturally belong in
`profile-metadata.conf` because they describe a profile rather than SNMP wire
configuration or tree placement. Discovery destination folders belong in
Discovery/UI preferences as stable folder IDs. Saved searches and availability
status should remain separate UI/runtime concepts. Reusable credentials require
a dedicated future USM/credential design and are not part of this format.
