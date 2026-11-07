# MIB Navigator 1.0.0-rc1 validation

Current redesign guidance: 2026-11-07. The authoritative development direction
is `docs/redesign-plan.md`; current Linux evidence and exact validation artifacts
are recorded in `docs/finish-review-validation.md` in the working tree.
A historical passing build or an old temporary install is not evidence that a
current package exists or has passed platform acceptance.

## Build prerequisites

- CMake 3.22+, a C++17 compiler and Qt 6.10+ with Core, Widgets, Network and Svg.
- Linux: the configured GCC/Clang and Qt development environment; Ninja is used
  for local development. Build on the VM's local disk, never on the NAS archive.
- Windows: MSVC x64 matching the installed Qt kit.
- macOS: AppleClang, Qt and the normal bundle deployment tools.
- Python is required for the deterministic loopback SNMP acceptance scenarios.

Use the actual CMake cache/toolchain rather than an old compiler minor version.
The patched libsmi, SNMP++ and crypto libraries remain independent targets.
Qwt/Graphs and Discovery have been removed.

## Configure, build, test, install, and package

```text
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure
cmake --install build-release --config Release --prefix <absolute-staging-path>
cmake -DSNMPB_PACKAGE_ROOT=<absolute-staging-path> -P cmake/VerifyReleaseTree.cmake
cpack --config build-release/CPackConfig.cmake -C Release
cmake -DSNMPB_PACKAGE_DIR=<archive-directory> -DSNMPB_EXTRACT_DIR=<temporary-extract-directory> -P cmake/VerifyReleaseArchive.cmake
```

The complete deterministic run includes the five historical libsmi diagnostics,
headless startup/restart, tolerance scenarios and scripted loopback SNMP. The
GitHub Actions matrix currently excludes `snmpb.launch-smoke`; local Linux
validation explicitly runs it. Test processes use isolated settings/collections.
Do not launch the desktop GUI or contact real devices as an implicit build check.

Verify the actual installed executable and all packaged MIB/PIB bytes against the
source/build. On Linux, run `ldd <installed-executable>` and require no missing
libraries. Installed offscreen startup/restart can exercise deployment paths
without launching a desktop window. Create one stable install for owner review;
prefer a local persistent location when it must survive a VM reboot. Do not reuse
a stale artifact or modify generated repository `manual-validation/` output
without authorization.

CPack produces platform archives from the explicit install rules. The CI matrix
builds, tests, stages, verifies, packages, extracts and verifies those archives.
Successful native archive validation requires the corresponding CI run; a local
Linux build does not establish Windows/macOS results. Do not push solely to
trigger CI without the owner's instruction.

On Windows, launch `bin/mib-navigator.exe` with its deployed Qt runtime/plugins.
On Linux, launch `bin/mib-navigator`; data is under `share/snmpb` and resolved
through the compiled relative install layout. On macOS, launch `MIB Navigator.app`;
the executable is `Contents/MacOS/MIB Navigator`, with resources/frameworks/plugins
in the bundle's standard locations. Qt's deployment support assembles the native
runtime. Packages are unsigned and macOS notarization is not provided.

## Runtime ownership and compatibility

Preferences retain the historical user-scope INI identity
`snmpb.sourceforge.net/SnmpB`. Credentials and diagnostic/parser/boot-counter
files retain their existing locations. Private profile definitions/indexes/caches
use application data storage. The user-visible configurable collection defaults
to `Documents/MIB Navigator/MIBs`; bundled MIB/PIB resources seed that collection.
The active Profile plan authorizes providers; old mibpaths/preload arrays do not
silently override it. See the development-tree `docs/settings-schema.md`.

Target history, reusable credentials and MIB Profiles are independent. There is
no saved-device migration. Existing `agents.conf` and other retired files remain
untouched. The package must contain no user profiles, credentials, settings,
logs, boot counters or generated parser state. `VerifyReleaseTree.cmake` checks
private filenames and development/debug artifacts as well as required resources.
Preserve source MIB bytes and investigate golden changes rather than rewriting
the historical expected diagnostics.

## Remaining acceptance and known limits

Desktop UX review is the final local step: inspect the tested build's controls,
keyboard behavior and resized dialogs; do not ask the owner to debug internals.
Use `manual-real-device-acceptance.md` only for separately authorized lab tests.
The optional implementation-check feature remains deferred, as do Query Macros
implementation and Watch-all/table expansion. Current SNMPv3 connection-bar
requests use the default empty context; its planned preference remains absent.

Existing full-application/libsmi leaks require disabling leak detection for broad
ASAN invalid-access workflows. Strict isolated widget checks should retain leak
detection; do not describe a broad run as leak-free. Existing compiler warnings
must remain visible. Parser shutdown may wait for a running uninterruptible parse.
Credential storage limitations remain documented in `security.md`. Translation
and native platform/desktop integration need their established release review.

The numeric CMake project version remains `1.0.0`; the release label is
`1.0.0-rc1`. Do not promote, commit, push, tag or publish without the owner's
instruction. Qt licensing/notices and release-owner name/trademark clearance
remain distribution responsibilities.
