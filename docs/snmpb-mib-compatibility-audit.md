# SnmpB MIB Compatibility Audit

## Executive Summary

Current MIB Navigator preserves the original core parser, configured search-path plumbing, recursive libsmi imports, persisted preloads, loaded-module/tree rebuilding, PIB candidate handling, and the useful distinction between physical candidate files and loaded module identities. It also adds substantially richer module metadata, deterministic value snapshots, profiles, catalog/download provenance, validation, and structured diagnostics.

The most important qualification is that neither the original nor current legacy preload path implements genuinely order-independent loading for vendor collections in which a filename differs from its declared module identity. The original implementation did **not** pre-index declarations, retry failures, or make multiple passes. Its discovery scan called `smiLoadModule(filename)` for every candidate in an unsorted directory listing, accumulating successful parses and recursively loaded imports in one libsmi process. This often made a later operation appear order-independent, but a cold import of `SYNOPTICS-ROOT-MIB` cannot be resolved by stock/patched libsmi from `synro.mib`: libsmi searches `SMIPATH` for the requested identity plus known suffixes, not for declarations inside arbitrary files. The subsequent preload reload resets libsmi and performs a single ordered pass, so the discovery side effects do not survive that reset.

The approved current worktree has restored custom-path modules to Inventory by snapshotting successfully parsed modules. That closes the clearest Inventory regression, including multiple declared identities from one file, but it does not make failed or not-yet-parsed vendor files discoverable by identity. A small declaration index plus dependency-aware/retry loading is therefore the minimum important restoration—not a parser replacement or architecture rewrite.

### Audit baseline and terminology

“Original” below means the last pre-Qt-6 implementation at Git revision `90f449b^`, whose `MibModule`, `MibViewLoader`, Preferences, and Qt Widgets Modules UI retain the historical SnmpB architecture. “Current” means the approved uncommitted worktree audited on 2026-08-13. Historical references use file plus class/function symbol because line numbers vary by revision; current references include approximate lines or symbols.

“Explicit” means present in persisted `mibpreloads`/`MibModule::Wanted`. “Imported” means loaded recursively by libsmi while parsing another module. This distinction matters because the old column label **Required** did not mean “required as a dependency.”

## Original Architecture

The original subsystem was centered on `MibModule` (`app/mibmodule.cpp`, `MibModule::MibModule`, `RebuildTotalList`, `Refresh`) and the process-global libsmi handle. Preferences stored search directories and preload strings in `QSettings` arrays. `MibViewLoader` loaded the explicit list and retained its module pointers for tree-membership checks (`app/mibview.cpp` at `90f449b^`, `MibViewLoader::Load`, `IsPartOfLoadedModules`). The Modules tab was a pair of `QTreeWidget`s and arrow buttons (`app/mainw.ui` at `90f449b^`, `UnloadedModules`, `LoadedModules`, `ModuleAdd`, `ModuleDelete`). There were no dedicated “load all” or “unload all” operations; extended selection/Ctrl+A plus the corresponding arrow implemented those user workflows.

Candidate discovery and active loading were related but distinct:

1. `ReadMibPaths()` read each `mibpaths/N/dir` string and passed the joined, ordered list directly to `smiSetPath` (`app/mibmodule.cpp` at `90f449b^`, `MibModule::ReadMibPaths`).
2. `RebuildTotalList()` enumerated each path with `QDir::Unsorted`, accepted conventional MIB/PIB names and extensions through `MibFilenameFilter`, and called `smiLoadModule(fn)` using the physical basename. It stored one `Total` row per accepted physical file, plus root OIDs from the single module returned by that load. It did not build a declared-name-to-file map (`MibModule::RebuildTotalList`, `MibFilenameFilter`).
3. Parsing imports caused the libsmi SMI grammar to call `findModuleByName(importedName)` and then `loadModule(importedName, parentParser)` (`libsmi/lib/parser-smi.y`, `import` production near line 1755). `loadModule` called `smiGetModulePath` (`libsmi/lib/smi-data.c`, `loadModule`, near line 4596).
4. For a plain module name, `smiGetModulePath` tried each search directory in order, the exact/lowercase requested name, and a fixed extension list (`""`, `.my`, `.smiv1`, `.smiv2`, `.sming`, `.mib`, `.txt`, `.yang`). It did not scan file contents (`libsmi/lib/common.c`, `smiGetModulePath`, lines 32-93).
5. `Refresh()` reread paths/preloads, regenerated `smi.conf`, called `InitLib(1)` (`smiExit`, `smiInit`, restore path), then called `MibViewLoader::Load(Wanted)` once in stored order (`app/mibmodule.cpp` at `90f449b^`, `Refresh`, `InitLib`; `app/mibview.cpp`, `MibViewLoader::Load`).
6. `RebuildLoadedList()` enumerated every `SmiModule` in libsmi, including recursively imported modules. `RebuildUnloadedList()` compared each candidate basename against the basename of each loaded module path (`app/mibmodule.cpp` at `90f449b^`, corresponding functions).
7. The MIB tree was dirtied/cleared by `MibViewLoader::Load`; later population traversed libsmi nodes and used the explicit module-pointer vector to decide membership (`app/mibview.cpp` at `90f449b^`, `Load`, `IsPartOfLoadedModules`, `BasicMibView::Populate`).

This architecture explains the original tolerance: every candidate was attempted, parser state was shared during discovery, imports were recursive, and an error in one file did not stop scanning later files. It does not establish a general dependency-order guarantee.

## Current Architecture

The legacy lifecycle remains active in current `MibModule`: path reading, candidate scan, persisted `Wanted`, whole-libsmi restart, preload loading, Loaded/Available list rebuild, and auto-load-by-root-OID remain recognizable (`app/mibmodule.cpp`, `RebuildTotalList` around line 177, `Refresh` around line 534, `RescanPath` around line 563). The current code still uses `QDir::Unsorted` and one `smiLoadModule(filename)` attempt per physical candidate.

The modernization adds complementary layers:

- `MibService` snapshots libsmi pointers into owned `MibModuleRecord`/tree values and collects operation-scoped diagnostics (`app/mibservice.cpp`, `loadModules`, `modulesFromFile`, `snapshotModule`, `treeSnapshot`; `app/mibrecords.h`, `MibModuleRecord`).
- The current discovery scan records **all** loaded module identities whose canonical `SmiModule::path` matches a candidate file, rather than only the module returned by `smiLoadModule` (`app/mibmodule.cpp`, `RebuildTotalList`; `app/mibservice.cpp`, `modulesFromFile`). `AvailableModuleRecords()` supplies those snapshots to Inventory (`app/mibmodule.h`; `app/snmpb.cpp`, MIB Library callbacks around lines 191-205).
- `MibLibraryService::inventory` combines bundled static scans, downloaded modules and provenance, catalog-only entries, and successfully parsed custom-path snapshots (`app/miblibrary.cpp`, `inventory`, around lines 321-374).
- `MibLibraryWidget` presents Inventory, dependency planning, Info, Profiles, catalog refresh/download, validation, and provenance (`app/miblibrarywidget.cpp`, constructor, `refresh`, `resolveSelected`, `showCurrentInfo`).
- Profiles call `EnsureLoaded` and apply `MibTreeFilterModel` visibility rather than physically unloading libsmi modules (`app/snmpb.cpp`, profile-selection callback around lines 233-247; `app/mibview.cpp`, `EnsureLoaded` around line 694; `app/mibtreemodel.cpp`, `MibTreeFilterModel`).

This is an intentional layering, not a replacement of libsmi. It is valuable, but it creates two meanings of “available”: the legacy Modules tab still shows unloaded physical files, while Inventory primarily shows declared identities and catalog records.

## Compatibility Matrix

| Capability | Original SnmpB | Current MIB Navigator | Status | Notes |
|---|---|---|---|---|
| Custom MIB search paths | Persisted ordered strings; passed to `smiSetPath` | Same, plus managed download path | Preserved | `MibModule::ReadMibPaths`; downloaded path append is current-only. Inventory’s earlier omission is fixed by `AvailableModuleRecords`. |
| Filename != module identity | Direct candidate load by filename worked; later identity import only worked if already loaded or identity-resolvable | Same libsmi behavior; snapshots retain the declared name/path after success | Preserved | Directly opening `synro.mib` works. Cold `smiLoadModule("SYNOPTICS-ROOT-MIB")` does not find `synro.mib`. |
| Multiple modules per file | libsmi could hold multiple `SmiModule`s; Loaded had one row per identity, but `Total` had one file row and root indexing used only the returned module | Loaded remains per identity; current snapshots enumerate all identities sharing the canonical file path | Improved | `MibService::modulesFromFile` fixes Inventory representation, not dependency resolution. |
| Order-independent loading | No formal guarantee; unsorted one-pass discovery plus shared parser state often masked ordering | Still no complete declaration index/retry/multipass | Regressed | Not a newly removed algorithm, but current Profiles/Inventory make declared-name loading more prominent and expose the inherited limitation as a user-visible failure. |
| Recursive imports | libsmi grammar recursively called `loadModule(importName)` | Same patched libsmi grammar | Preserved | `libsmi/lib/parser-smi.y`, `import` production. |
| Available-files view | One row per accepted physical basename not represented by a loaded module path | Legacy Modules tab retains this; Inventory instead uses identities/catalog entries | Preserved | The two views answer different questions and should remain clearly named. |
| Loaded-modules view | One row per libsmi `SmiModule`, including imports; Module/Required/Language/Path | Legacy Modules tab retains it; Inventory adds a separate identity-centric view | Preserved | `MibModule::RebuildLoadedList`. |
| Required=yes/no | `yes` iff `Wanted.contains(declaredModuleName)` | Same expression | Regressed | It is explicit-request bookkeeping, not dependency/reference state. Because `Wanted` is often a filename, the label can be false even for an explicitly selected file. Do not reuse this test for Profiles. |
| Preload behavior | `mibpreloads` strings loaded once after full libsmi restart | Same legacy flow; Profiles add non-destructive requested loads | Preserved | `ReadMibPreloads`, `Refresh`, `MibViewLoader::Load`. |
| Unload-all | No dedicated API/button; select all Loaded, remove their path basenames from `Wanted`, save, whole restart | Legacy workflow remains; Profiles intentionally do not unload | Intentionally Replaced | Profile switching uses visibility, avoiding unsafe dependency unload. |
| Reload-all | Select all Available files, append basenames to `Wanted`, save/restart/load once | Legacy workflow remains | Preserved | It reparses persisted selections, not all candidates, and is not a multipass operation. |
| Individual unload | Selection removed the loaded row’s physical basename from `Wanted`; whole refresh reset all state | Legacy Modules behavior remains; no per-module libsmi unload in Profiles | Intentionally Replaced | It never safely decremented dependency references; the whole reset was the safety mechanism. |
| Tree rebuild | Loader dirtied/cleared views; tree traversed current libsmi graph | Value-snapshot model rebuilt after load; profile proxy filters visible modules | Improved | `MibViewLoader::Load`; `MibService::treeSnapshot`; `MibTreeFilterModel`. |
| Root Node metadata | Module-identity node name | Name retained in legacy info; Inventory shows numeric Module OID | Preserved | Current record has both `rootName` and `rootOid`; Inventory currently labels/displays OID. |
| Requires metadata | Deduplicated imported module names | `MibModuleRecord::imports`; dependency pane/plan | Improved | Current Inventory emphasizes actionable dependency state. |
| Organization | Displayed from `SmiModule::organization` | Displayed with presentation-only paragraph normalization | Improved | `LoadedMibModule::PrintProperties`; `MibLibraryWidget::showCurrentInfo`. |
| Contact Info | Displayed from `SmiModule::contactinfo` | Displayed; meaningful line structure preserved | Improved | Current normalization affects display only. |
| Reference | `SmiModule::reference` existed in libsmi but original module Info did **not** render it | Snapshot and Inventory render Reference | Improved | The manually remembered Reference/URL is not supported by original `LoadedMibModule::PrintProperties` at `90f449b^`; it may be confusion with node/file UI. |
| Revision metadata | Only first revision date (“Last revision”) | Latest date plus complete dated revision descriptions | Improved | `MibService::snapshotModule`; `MibLibraryWidget::showCurrentInfo`. |
| Path metadata | Loaded table showed `SmiModule::path` | Legacy table plus Inventory File Information/local provenance | Improved | Current separates module metadata from file provenance. |
| Language metadata | Loaded table showed SMIv1/SMIv2/SMIng/SPPI | Retained in `MibModuleRecord` and legacy table; not presently a primary Inventory Info row | Preserved | `LoadedMibModule::GetMibLanguage`; `MibService::languageName`. |
| PIB handling | Filename filter accepted `-PIB`, `.pib`/`.PIB`; default path included PIB directory | Shared `MibCandidateFilter`; executable-relative `pibs` default | Preserved | `app/mibcandidatefilter.*`; `Preferences::DefaultMibPaths`. |
| Failure diagnostics | Error callback/log plus modal listing every failed filename | Structured collector/logging and concise count modal | Improved | Current `RebuildTotalList`; `MibDiagnosticCollector`; giant modal intentionally removed. |
| Search-path persistence | `QSettings` array `mibpaths`, ordered, verbatim | Same schema through `PreferencesSettings`; managed path appended at runtime | Preserved | `app/preferencesettings.cpp`, `load`/`save`; `MibModule::ReadMibPaths`. |
| Search-path validation | Nonexistent/unreadable paths silently produced no candidates; libsmi followed path order | Same basic behavior | Missing | There is still no explicit stale/invalid configured-path status. |
| IANA catalog/download | Absent | Catalog cache, dependency planning, secure install checks, provenance | Improved | `app/miblibrary.cpp`; `app/miblibrarywidget.cpp`. |
| MIB Profiles | Absent | Persistent profiles, effective membership, tree visibility filtering | Improved | `app/mibprofile.*`; integration in `app/snmpb.cpp`. |

## Exact Original Load Sequence

### Startup/rescan

1. `MibModule` connected its error handler, called `InitLib(0)` (`smiInit`, error flags/handler), then `RescanPath()` (`app/mibmodule.cpp` at `90f449b^`, constructor and `InitLib`).
2. `RescanPath()` called `ReadMibPaths()`, `RebuildTotalList()`, then `Refresh()`.
3. `ReadMibPaths()` read the `QSettings` `mibpaths` array in stored order and replaced libsmi’s path through `smiSetPath`.
4. `RebuildTotalList()` walked each directory in path order but each directory’s files in `QDir::Unsorted` order. It filtered likely MIB/PIB filenames and called `smiLoadModule(physicalBasename)` once per candidate.
5. During a successful parse, libsmi loaded imports recursively. A dependency already parsed in the same process was returned by `findModuleByName`; otherwise libsmi searched for a file named after the imported identity. SnmpB did not intervene in that resolution.
6. Fatal/error state caused that file to be omitted from useful root-OID metadata and included in the failure warning. Scanning nevertheless continued. `Total` was finally sorted by physical filename.
7. `Refresh()` reread the same paths. If changed, it recursively initiated a rescan. It then read `mibpreloads`, regenerated a separate `smi.conf` representation, and called `InitLib(1)`.
8. `InitLib(1)` saved the string path, called `smiExit()` and `smiInit(NULL)`, restored the path and error configuration. This discarded every discovery-loaded module and all helpful parse order/state.
9. `MibViewLoader::Load(Wanted)` made one sequential `smiLoadModule(preloadString)` call per persisted entry. Successful explicit modules were retained for view membership; libsmi also retained recursive imports.
10. `RebuildLoadedList()` enumerated all libsmi modules. `RebuildUnloadedList()` computed unloaded physical files by comparing candidate basename with every loaded module path basename. The two widgets were sorted for display.

### Load/unload interaction

- Adding selected Available rows appended their **physical filenames** to `Wanted`; Preferences saved the full array and immediately called `Refresh()` (`MibModule::AddModule`; `Preferences::Save`).
- Removing selected Loaded rows removed each row’s **path basename** from `Wanted`; save/refresh again reset and reconstructed all libsmi state (`MibModule::RemoveModule`).
- Dependencies were not individually freed. Removing an explicit module could also remove an entry whose file contains several identities; dependencies still needed by another explicit preload were recursively reconstructed on refresh.
- “All” was selection behavior, not a different algorithm. Reload-all loaded the selected filenames once in the `Wanted` order; it did not rerun candidate discovery after each success and did not retry earlier failures.

## Order Independence Analysis

### What the source proves

The exact mechanism that produced the *appearance* of order independence was the combination of:

1. exhaustive physical-file attempts during `RebuildTotalList`,
2. a shared process-global libsmi module graph during that scan,
3. libsmi’s recursive import loading, and
4. continued scanning after failures.

If the provider had already been parsed, `findModuleByName("SYNOPTICS-ROOT-MIB")` satisfied the child import regardless of the provider’s physical filename. If the provider filename itself matched an identity/suffix that libsmi searched, recursion also worked regardless of directory enumeration order.

For the specific counterexample, a cold parse of `bayStackArpInspection.mib` importing `SYNOPTICS-ROOT-MIB` cannot discover `synro.mib` by declaration. `smiGetModulePath` does no content scan and has no alias index (`libsmi/lib/common.c`, `smiGetModulePath`). If the child is attempted first, it may fail; when `synro.mib` is attempted later, original SnmpB does not retry the child. The final `Refresh()` then restarts libsmi, so discovery-loaded aliases vanish. Preloads are loaded once and have the same order sensitivity.

Therefore the manual observation “unload all and reload all without manually ordering dependencies” is compatible with the source—filesystem/UI ordering or prior matching dependencies can make that collection succeed—but it is not evidence of a general algorithmic guarantee. There was no pre-index, retry, multipass, topological sort, or declaration alias registration in original SnmpB.

### Current preservation

Current code preserves the same tolerant brute-force scan and recursive libsmi behavior, so it preserves the favorable cases. It does **not** preserve strict order-independent vendor loading because strict order independence did not exist to preserve. More importantly, current identity-centric Profiles can request a declared module name directly (`MibService::loadModules` via `LoadPreferredModules`/`EnsureLoaded`), which more readily hits the filename mismatch. Current `modulesFromFile` indexes identities only *after successful parsing* and only for Inventory; it is not consulted by libsmi import resolution.

The recent “MIB search-path resolution begin” flood was a misplaced diagnostic, not a loading-semantic change. It is now emitted once at the start of `RebuildTotalList` (`app/mibmodule.cpp`, `RebuildTotalList`). The concise failure count similarly improves presentation without changing which files parse.

## Available Files Versus Loaded Module Identities

Original **Available MIB modules** represented unloaded physical candidate basenames. `Total` had one entry for each accepted readable file even if parsing returned no module (except error handling limited its OID data); `RebuildUnloadedList` removed a file from display when any loaded `SmiModule::path` had the same basename. It did not represent a complete declaration inventory.

Original **Loaded MIB modules** represented the declared identities present in libsmi. `smiGetFirstModule`/`smiGetNextModule` produced one row per `SmiModule`, including recursive imports and multiple modules parsed from a single file. Each row associated its identity with `SmiModule::path`. Thus one physical file could suppress one Available row while producing multiple Loaded rows.

Current legacy Modules retains this model. Current Inventory is intentionally identity-centric: static content scanning finds declarations in bundled/downloaded files, catalog records represent remotely available identities, and `AvailableModuleRecords` contributes every successfully parsed custom-path identity associated with its actual path (`MibLibraryService::inventory`; `MibService::modulesFromFile`). This is an improvement, but failed custom candidates are absent rather than represented as physical files with diagnostics.

## Required = yes Analysis

Original and current `RebuildLoadedList()` compute:

```cpp
QString required = Wanted.contains(lmodule.name) ? tr("yes") : tr("no");
```

This tests application bookkeeping only. It does not inspect imports, libsmi flags, reference counts, or dependency relationships. Semantically, `yes` means “the declared identity exactly matches a persisted explicit preload string.” Recursively loaded dependencies normally show `no`.

There is a historical mismatch: Available selection appends a physical basename to `Wanted`, while the test compares against a declared identity. A file such as `synro.mib` can therefore be explicitly selected but its `SYNOPTICS-ROOT-MIB` row still display `no`. Multiple identities in one explicitly selected file have the same problem. The column is not a reliable source of truth and should not be carried into Profiles under the name Required. Profiles should explicitly distinguish **profile member/requested** from **dependency-loaded**, based on normalized identity records and a dependency plan.

## Module Info Audit

Original `LoadedMibModule::PrintProperties` (`app/mibmodule.cpp` at `90f449b^`) rendered:

| Original field | Source | Current Inventory | Classification |
|---|---|---|---|
| Name | `SmiModule::name` | Module | Preserved |
| Last revision | first `smiGetFirstRevision(module)->date` | Last Updated plus full Revision History | Improved |
| Description | `SmiModule::description` | Description, display-normalized | Improved |
| Root node | `smiGetModuleIdentityNode(module)->name` | Record retains name and OID; Inventory displays Module OID | Preserved |
| Requires | unique adjacent `SmiImport::module` names | Imports/dependency plan | Improved |
| Organization | `SmiModule::organization` | Organization | Preserved |
| Contact Info | `SmiModule::contactinfo` | Contact Information | Preserved |
| Reference | Not rendered | `SmiModule::reference` snapshot | Improved |
| Language | Not in Info; present in Loaded table | Snapshot/legacy Loaded table | Preserved |
| Path | Not in Info; present in Loaded table | File Information path | Improved |
| Revision descriptions | Not rendered | Complete dated history | Improved |
| Copyright/header URL | No special extraction/rendering | Only formal module Reference and catalog Source URL | No Longer Relevant |

The known claim that original Module Info displayed Reference/URL is not verified by the audited source. libsmi carried `SmiModule::reference`, but `PrintProperties` stopped after Contact Info. A URL might appear inside free-text description/contact data or elsewhere in the editor/node panes; it was not a dedicated module Info field.

Current values originate in `MibService::snapshotModule` (`app/mibservice.cpp`, around lines 113-154) and are rendered in `MibLibraryWidget::showCurrentInfo` (around lines 589-638). The current File Information group additionally separates origin, catalog revision, filename, local path, provider, source URL, timestamp, checksum, and failure state according to applicability. Presentation normalization does not mutate source files or stored snapshot strings.

## Search Paths and Persistence

Original Preferences initialized a missing `mibpaths` array from compile-time `DEFAULT_SMIPATH`; it displayed and edited strings verbatim, preserved their order, and wrote them as `mibpaths/<index>/dir` (`app/preferences.cpp` at `90f449b^`, `Init`, `MibPathRefresh`, `MibPathReset`, `Save`). Path edits took effect when Preferences saved and invoked `MibModule::Refresh`; a changed libsmi path triggered a rescan. Path order mattered both for candidate traversal and libsmi’s first readable identity match. Relative strings were not normalized by SnmpB; they were interpreted by Qt/libsmi relative to the process working directory. Invalid/stale directories were silently empty from the candidate scanner and eventually produced module-not-found diagnostics only when a requested identity could not resolve.

Current persistence uses the same compatible arrays via `PreferencesSettings::load/save` (`app/preferencesettings.cpp`, lines 49-50 and 77-78). Defaults are now computed from installed/executable-relative `mibs` and `pibs` locations (`app/preferences.cpp`, `Preferences::DefaultMibPaths`, around lines 332-369), and `MibModule::ReadMibPaths` appends the managed downloaded directory before `smiSetPath` (around lines 477-493).

The observed stale `build-graph-presentation/.../mibs` value is a persisted-settings issue, not a current compiled-default rule: existing nonempty arrays are retained, while defaults are only installed when `mibpaths/size == 0` (`Preferences::Init`, around lines 177-181). The application should eventually surface and offer repair for nonexistent paths, but silently rewriting user-configured custom paths would be unsafe.

## Features Lost or Regressed

### Release-blocking

1. **No robust identity-to-file resolution for mismatched vendor filenames.** Identity-centric load requests can fail even though a provider exists in a configured directory. This blocks claiming reliable messy-vendor compatibility. It is inherited from the original algorithm but is more visible in current Profiles and recent vendor scans. Evidence: `libsmi/lib/common.c::smiGetModulePath`; `app/mibmodule.cpp::RebuildTotalList`; `app/mibservice.cpp::loadModules`.

### Important

1. **No retry/multipass after discovery failures.** A consumer encountered before an alias-named provider is not retried after the provider succeeds. Failed custom files are also absent from identity-centric Inventory. Evidence: the single loops in original/current `MibModule::RebuildTotalList`.
2. **Required column is semantically unreliable.** It compares preload strings to identities even though selection stores filenames. It should be relabeled/replaced rather than trusted. Evidence: `MibModule::AddModule`, `RemoveModule`, `RebuildLoadedList`.
3. **Stale/invalid path state is invisible.** Persisted build/runtime paths can remain indefinitely with no per-path status. Evidence: `Preferences::Init`, `MibPathRefresh`; `MibModule::ReadMibPaths`.

### Nice-to-have

1. Inventory could expose failed physical candidates and their latest diagnostics without pretending they are known module identities.
2. Inventory could display retained language and root-node name alongside its richer metadata when useful.
3. The legacy Available/Loaded terminology could explicitly say **files** versus **module identities**.

## Features Improved in MIB Navigator

- Inventory unifies bundled, downloaded, custom successfully parsed, and catalog-only module identities (`MibLibraryService::inventory`).
- Current custom-path snapshots retain all declared identities sharing a physical file (`MibService::modulesFromFile`; `MibModule::AvailableModuleRecords`).
- Module records safely preserve name, path, language, root name/OID, organization, contact, description, formal reference, all revisions, and imports outside transient libsmi pointers (`MibService::snapshotModule`).
- Info provides complete revision history and display-only whitespace normalization while preserving source metadata (`MibLibraryWidget::showCurrentInfo`).
- Dependency planning, IANA catalog/cache, download with identity/checksum/atomic-write validation, provenance metadata, and immutable bundled-file protection did not exist originally (`app/miblibrary.cpp`, `resolve`, `install`; `app/miblibrarywidget.cpp`).
- Profiles provide persistent reusable module sets and non-destructive per-profile tree visibility (`app/mibprofile.*`; `MibTreeFilterModel`).
- Tree data is an owned model snapshot rather than direct widget ownership of libsmi pointers (`MibService::treeSnapshot`; `MibTreeModel`).
- Diagnostics are operation-scoped and the scan modal is a concise count instead of an enormous filename list (`MibDiagnosticCollector`; `MibModule::RebuildTotalList`).

## Recommended Restoration Plan

1. **Build a lightweight declaration index before active loading.** Scan accepted configured-path files for top-level module declarations (reuse/harden `MibImportScanner`) and record `declared identity -> canonical physical path`, allowing multiple identities per file and conflicts. Do not parse/replace libsmi semantics.
2. **Resolve requested identities through that index.** When libsmi cannot resolve an import/request by conventional name, load the indexed physical provider path, verify that libsmi produced the requested identity, then retry the consumer. Preserve configured path precedence and report ambiguity rather than choosing silently across conflicts.
3. **Use bounded multipass retry for candidate/preload batches.** Retry only failures for which the pass loaded at least one new provider; stop at a fixed point and retain deterministic diagnostics. This covers forward alias dependencies and cycles without indefinite loops.
4. **Unify explicit/dependency state by declared identity.** Store or derive normalized explicit identities while keeping backward compatibility with filename-based `mibpreloads`. Replace **Required** with two truthful concepts such as **Requested** and **Loaded as dependency**.
5. **Feed the same index/results to Inventory, Profiles, and loader diagnostics.** Inventory may show physical candidates with unknown/failed identity separately. Avoid parallel scanners with divergent precedence rules.
6. **Add deterministic vendor fixtures.** Cover consumer-before-provider, filename/identity mismatch, multiple identities per file, conflicts across path order, cold restart/preload reconstruction, recursive dependencies, and fixed-point failure. Keep the five libsmi golden tests unchanged.
7. **Add path health presentation.** Show missing/unreadable/stale configured paths and an explicit reset/repair action; never silently delete custom paths.

This is a narrow resolver/index addition around the existing patched libsmi target. It does not require changing the parser, storage format wholesale, or Profiles’ visibility design.

## Do Not Restore

- Do not restore giant modal dialogs listing every failed file; keep concise UI plus detailed diagnostics/logs.
- Do not implement direct per-module `smiFreeModule` unloading or dependency reference manipulation. The historical implementation avoided it and used a whole libsmi restart; Profiles should continue using visibility filtering.
- Do not replace identity-centric Inventory with the old filename-only Available list. Preserve both concepts with clear labels where needed.
- Do not remove Profiles, value snapshots, IANA catalog/download, provenance, validation, or immutable bundled-module handling.
- Do not treat the old **Required** calculation as authoritative or reproduce its misleading filename/identity comparison.
- Do not restore compile/build-directory defaults or silently persist generated build paths. Keep executable/install-relative defaults and expose stale persisted paths for user repair.
- Do not broaden candidate parsing by blindly treating every readable file as a MIB; retain a documented, shared candidate filter and explicit diagnostics.
- Do not change or regenerate patched libsmi parser/scanner sources merely to solve alias lookup; resolve aliases in the application layer first.

## Conclusion

MIB Navigator retains most of the original SnmpB subsystem’s valuable behavior and adds strong modern capabilities. The audit does not find a lost original order-independent loader; it finds a tolerant but incidental one-pass mechanism whose success depended on filename conventions and encounter state. The serious compatibility work is therefore to make the behavior users reasonably inferred from original SnmpB—messy vendor collections loading without manual ordering—an explicit, deterministic property of the current resolver while retaining libsmi and the new Inventory/Profile architecture.
