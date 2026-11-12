MIB Navigator 1.0.0-rc2 release-candidate archive
=====================================

Extract the complete archive and keep its executable, runtime dependencies,
resources and documentation together. Launch the executable for your platform:

  Windows: bin/mib-navigator.exe
  Linux:   bin/mib-navigator
  macOS:   MIB Navigator.app

On Linux this archive requires compatible system Qt and other shared libraries;
it does not bundle a complete Linux runtime. Validate dependencies with ldd and
use the documented Qt 6.10+ runtime. Native Windows/macOS deployment requires
the corresponding platform package and acceptance checks.

Preferences and credentials use the historical SnmpB configuration identity.
Private indexes/profiles/caches use application data storage. Actual working
MIB files live in the configurable user-visible MIB collection, normally
Documents/MIB Navigator/MIBs. Runtime state is not written into this package.
Graphs, Discovery and saved-device management have been retired. Existing
agents.conf is left untouched; credentials are entered through Settings.

Bundled MIB/PIB seeds are in bin/mibs and bin/pibs on Windows,
share/snmpb/mibs and share/snmpb/pibs on Linux, and the app bundle's Resources
on macOS. License materials are under share/snmpb/licenses in the package.
See release-notes-1.0.0-rc2.md, security.md and release-candidate-validation.md
for capabilities, platform checks and known limitations.

This release is intended for release-candidate evaluation. Back up existing
SnmpB configuration before testing an upgrade. Perform device operations only
against explicitly authorized lab targets and use safe objects for SET testing.
