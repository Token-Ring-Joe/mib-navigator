MIB Navigator 1.0.0-rc1 portable package
=====================================

Extract the complete archive to a writable or read-only application folder and
run bin/mib-navigator.exe. Keep bin, plugins, share, and the documentation files
together. User preferences, profiles, credentials, graphs, logs, and other
runtime state are stored in the current user's Qt configuration location; they
are not written into this package.

The package contains bundled MIBs and PIBs under bin/mibs and bin/pibs. License
materials are under share/snmpb/licenses. See release-notes-1.0.0-rc1.md,
security.md, and release-candidate-validation.md for changes, platform details,
credential-storage limitations, and other known limitations.

This release is intended for release-candidate evaluation. Back up existing
SnmpB configuration before testing an upgrade, and use only lab-safe objects
for SET testing.
