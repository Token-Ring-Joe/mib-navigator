# MIB Library, downloader, and dependency resolver V1

Patched libsmi remains the authoritative semantic parser and diagnostic source.
The Library's lightweight scanner reads only module identities and `IMPORTS`
module names so it can plan dependencies before every dependency is present.

## Storage and search paths

Downloaded content is stored in the configurable user-visible MIB root:

```text
<DocumentsLocation>/MIB Navigator/MIBs/Unassigned/
  <downloaded MIB files>
```

`Standards`, `Unassigned`, and vendor/product trees are added to runtime
discovery independently of `mibpaths`. Existing configured paths remain untouched
because the historical array does not distinguish an old managed path from a
deliberately configured custom directory. The current executable-relative
bundled paths remain dynamically computed by Preferences. Installation files
are never written or replaced; missing working copies are initialized beneath
`Standards`.

## Catalog and sources

Catalogs are UTF-8 JSON with `version: 1` and an `entries` array. Entries carry
`sourceId`, `sourceName`, `category`, `moduleName`, `revision`, `url`,
`filename`, `sha256`, `licenseUrl`, and `imports`. Filenames must be leaf names,
URLs must be HTTP(S), and optional SHA-256 values must contain 64 hexadecimal
characters. The cache location is `cache/catalog-v1.json`.

V1.1 enables bundled inventory, the per-user downloaded library, and one
built-in online provider: `iana` (display name `IANA`). IANA is the owner of
the protocol-parameter registries. The provider retrieves the public HTTPS
index at `https://www.iana.org/protocols`, reads only its
**IANA-Maintained MIBs** section, and accepts only HTTPS artifact links on
`www.iana.org` below `/assignments/`. These are standalone SMI modules.

The index supplies module identity and authoritative artifact URL. Revision is
read from `LAST-UPDATED` after download. The index publishes no artifact
checksum, so MIB Navigator records its own downloaded SHA-256 in provenance.
Refresh is user initiated; the normalized catalog, source URL, provider, and
UTC refresh time are atomically cached. Refresh failure never overwrites a
valid cache, which remains browsable offline.

IANA's index links name assignment landing aliases. In August 2026 those HTTPS
aliases returned an HTTP `Location` for the module artifact, which Qt correctly
rejected as an insecure redirect. The provider normalizes each discovered
`/assignments/<slug>` link to IANA's canonical HTTPS
`/assignments/<slug>/<slug>` artifact URL. Transport redirects are followed
manually only after resolving relative targets. HTTPS downgrades, loops, and
more than five redirects remain rejected; TLS errors are never ignored.

The RFC Editor is authoritative for RFC documents, but its index does not map
SMI module identities to embedded RFC sections. The existing interactive RFC
extractor is UI/file-dialog coupled. V1.1 therefore does not guess an IETF/RFC
mapping or advertise a provider that cannot reliably find a requested module.
A deterministic extraction service plus a reviewed mapping is deferred.

## Download and validation

`QtMibDownloadTransport` uses `QNetworkAccessManager`, TLS validation, safe
redirects, cancellation, a timeout, progress, and an 8 MiB limit. Tests use the
transport interface and never access the Internet. Content is checksum-checked,
identity-scanned, staged in the user cache, semantically loaded by patched
libsmi, then atomically installed with `QSaveFile`. Separate versioned JSON
provenance records retain source, URL, timestamp, revision, filename, path, and
computed checksum. Failed validation leaves no installed module.

## Dependency planning

The resolver keys by exact SMI module name. It sorts roots and imports,
deduplicates downloads, visits dependencies before dependents, marks cycles,
and classifies known bundled/installed modules, catalog-backed downloadable
modules, and unresolved names. It does not invent aliases. `Download Missing &
Load` downloads the dependency-first plan, validates and installs each module,
rescans the runtime path, and asks the existing MIB module service to load the
original targets.

## Editor Verify

Verify now stages the exact current editor buffer beside the opened file before
calling libsmi. Unsaved changes therefore reach the authoritative parser while
relative dependency lookup remains compatible. Existing structured diagnostics
and double-click line navigation are retained.

Explicit validation has three named levels: **Errors** maps to libsmi level 3,
**Errors + Warnings** maps to level 5 and is the default, and **Full Review**
maps to level 9 and enables recursive imported-module diagnostics. Normal MIB
loading remains at its historical tolerant level 3. Explicit download
validation restores the normal handler, flags, and level afterward. libsmi's
error callback provides source and line but no column, so no column is
fabricated.

## Limitations

- Only IANA's standalone maintained modules are discoverable online in V1.1;
  RFC-embedded modules remain deferred.
- Update Available is not inferred when revisions are absent or incomparable.
- Historical `mibpaths` entries are preserved because their ownership cannot be
  migrated safely without new metadata.
- libsmi is process-global, so semantic validation remains serialized on the UI
  thread after asynchronous download completion.
- Main-window geometry restoration should be hardened separately: saved
  geometry must intersect a screen available to the current desktop/RDP
  session, otherwise the window should be recentered. This is intentionally
  outside the downloader workstream.
