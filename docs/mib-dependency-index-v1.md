# MIB Dependency Index V1

MIB Navigator stores its application-owned dependency index at:

```text
<ApplicationLocalData>/mibs/dependency-index-v1.json
```

The index is read without rewriting it and is written atomically with `QSaveFile` only after a material scan/check update. It contains value data only; libsmi pointers are never persisted.

The shared resolver uses the stable legacy application storage identity
`SnmpB`, independent of helper/test executable names. Acceptance and tests
must pass an explicit isolated index path and never populate the production
index implicitly. Missing, zero-byte, malformed, and unsupported-schema files
are reported as distinct states and never trigger semantic startup scanning.

## Schema

The root contains `schemaVersion: 1`, `scannerVersion: 2`, a monotonic
`generation`, `files`, and `profileChecks`. A scanner-version change retains
provider knowledge for startup but marks cached file/profile results stale so
the next explicit dependency check refreshes declarations.

Each file record contains:

- canonical physical `path`
- configured search-path `precedence`
- physical `filename`
- byte `size` and modification time (`modifiedMsecs`)
- SHA-256 from the last content scan
- `modules`, mapping every declared identity in the file to its direct IMPORTS
- latest `checkState`, concise `diagnostic`, and `lastCheckedUtc`

Provider lookup selects the lowest configured path precedence. Multiple providers at that same precedence are reported as ambiguous; providers in later paths remain alternatives but do not override the earlier path.

Each profile check contains its definition signature, dependency-index generation, effective modules, automatic dependencies, unresolved identities, final classified failure summaries, check time, and elapsed time. A definition edit or index-generation change makes the cached result stale.

## Change detection

Size and modification time provide the cheap unchanged-file test. New or potentially changed files are read, SHA-256 hashed, and declaration-scanned. Deleted files are removed. An unchanged check reuses persisted declarations and does not hash or parse the file again.

The lightweight scanner discovers declarations and direct imports; it does not establish semantic validity. Patched libsmi verifies the selected physical provider and the requested identity must actually appear after loading.

## Bounded loading

The resolver attempts each pending identity once per pass. Successful identities leave the pending set. Only failures continue to the next pass. Processing stops immediately after a complete pass produces zero successes. A defensive maximum of `unique pending identities + 2` passes is also enforced.

Final failures are classified as missing provider, ambiguous provider, parser/semantic failure, or dependency unresolved. Transient failures that succeed on a later pass are not retained as final failures.

Full change discovery and verification runs only from **Check Dependencies** (or after a successful managed download). Startup reads cached knowledge and marks stale profile results as **Dependencies need checking** rather than running the multipass analysis.
