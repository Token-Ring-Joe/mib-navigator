# MIB Navigator 1.0.0-rc1 validation

## Build prerequisites

- CMake 3.22 or newer and a C++17 compiler.
- Qt 6.10 or newer with Core, Gui/Widgets, Svg, and LinguistTools development
  files. The validated Windows RC uses Qt 6.11.1.
- Windows: MSVC x64 matching the installed Qt kit.
- Linux: GCC/Clang plus Qt development packages and normal socket/thread system libraries.
- macOS: AppleClang, Qt development files, and standard bundle deployment tools.

No external Qwt package is used; the official Qwt 6.3.0 source is vendored and
built as a presentation-only static target.

## Configure, build, test, install, and package

```text
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release --config Release
ctest --test-dir build-release -C Release -E snmpb.launch-smoke --output-on-failure
cmake --install build-release --config Release --prefix <absolute-staging-path>
cpack --config build-release/CPackConfig.cmake -C Release
cmake -DSNMPB_PACKAGE_ROOT=<absolute-staging-path> -P cmake/VerifyReleaseTree.cmake
cmake -DSNMPB_PACKAGE_DIR=<archive-directory> -DSNMPB_EXTRACT_DIR=<temporary-extract-directory> -P cmake/VerifyReleaseArchive.cmake
```

On Windows, run these commands from the appropriate Qt/MSVC developer
environment. CPack produces `MIB-Navigator-1.0.0-rc1-windows-x64.zip`. It uses the
same explicit install rules as the verified staging tree and Qt's supported
deployment helper; no Qt installation path is hard-coded.

Linux CPack produces
`MIB-Navigator-1.0.0-rc1-linux-<architecture>.tar.gz`. macOS CPack produces
`MIB-Navigator-1.0.0-rc1-macos-<architecture>.tar.gz`. The CI matrix creates,
extracts, verifies, and uploads these archives for manual inspection; native
package validation is established only by a successful CI run containing
those steps.

On macOS, the application target installs as `snmpb.app`; its executable,
resources, deployed Qt frameworks, and platform plugins reside under
`Contents/MacOS`, `Contents/Resources`, `Contents/Frameworks`, and
`Contents/PlugIns`. MIB/PIB data is in `Contents/Resources`, and Qwt is
statically linked. Qt's supported deployment script assembles the bundle.
Mutable configuration remains in the user's configuration location, outside
the bundle. This RC is unsigned and unnotarized, and GUI launch has not been
validated by automated CI. On Linux, data installs beneath
`${CMAKE_INSTALL_DATADIR}/snmpb`, located at runtime through the compiled
relative install layout.

## Runtime files and first run

Preferences use user-scope INI `QSettings` with the established organization
domain `snmpb.sourceforge.net` and application name `SnmpB`. Keeping that
identity preserves existing settings. The directory containing that INI owns
all sibling runtime files:

- `agents.conf`, `device-tree.conf`, `profile-metadata.conf`, `graphs.conf`
- `credential-identities.conf`, `community-credentials.conf`,
  `credential-bindings.conf`, and `usm_users.conf`
- `smi.conf`, `boot_counter.conf`, and `log.conf`

Missing files are a normal first-run state. Repositories return empty/default
records and create files only through their established write paths. The
installation directory is not used for mutable configuration.

Bundled MIB and PIB defaults are installation-relative. Lookup remains
nonrecursive and ordered as before: platform data roots first where applicable,
then executable-relative `mibs` and `pibs`. User-configured paths and preload
semantics are unchanged. No source-tree `SMIPATH` is required at runtime.

## Compatibility and privacy

Legacy `agents.conf` records without stable IDs, selected profile names,
device-tree v1, profile metadata v1, name-based graph references, absent
reusable-credential stores, existing `usm_users.conf`, and historical
Preferences remain covered by deterministic repository tests. Reads do not
perform an unconditional destructive migration.

Each package is assembled exclusively from install rules. It must not contain
profiles, credentials, device trees, graphs, logs, preferences, boot counters,
or loaded-MIB state. `cmake/VerifyReleaseTree.cmake` enforces these exclusions
and rejects common build/debug artifacts.

## Known limitations and release policy

- Community credentials are plaintext-equivalent; USM password bytes are
  encoded, not cryptographically encrypted. See `security.md`.
- Native Linux and macOS archive validation requires the updated CI packaging
  steps to pass; successful build/test/install evidence alone is not an archive
  validation claim.
- Qt distribution licensing and corresponding notices/source obligations need
  release-owner review before public distribution.
- Ukrainian is the only application translation currently bundled.
- Startup MIB scanning intentionally accepts `.txt` and unusual/extensionless
  vendor files for compatibility; full paths identify failed candidates.
- Remaining BasicMibView compatibility dialogs and the ordinary Query callback
  architecture are maintenance concerns, not demonstrated correctness defects.

Use `manual-real-device-acceptance.md` for authorized network acceptance. The
automated suite deliberately does not contact SNMP devices.

The numeric CMake project version remains `1.0.0`; the canonical release label
is `1.0.0-rc1` and feeds the About display and CPack filenames. The established
QSettings organization/application identity remains unchanged.

Public release under the "MIB Navigator" name requires release-owner
name/trademark clearance. Promote to
`1.0.0` only after native package and manual device acceptance gates pass.
