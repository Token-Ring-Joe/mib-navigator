# MIB Navigator 1.0.0-rc1 release notes

MIB Navigator 1.0.0-rc1 is the first controlled release candidate of the Qt 6
modernization. It is intended for manual acceptance testing, not yet for broad
production deployment.

## Highlights

- Modern Qt 6 user interface and CMake build with Windows, Linux, and macOS CI.
- Preserved bundled, patched libsmi behavior for tolerant real-world MIB parsing.
- Persistent Device Manager with folders, stable profile identity, metadata,
  tags, preferred MIBs, duplication, and reusable credential references.
- Secret-free portable profile import and export.
- Explicit request configuration and context ownership, asynchronous table and
  instance operations, and improved Discovery workflows.
- Bounded value-based Trap History with MIB-aware presentation.
- Value-based MIB browser/loader models and attributable diagnostics.
- Restored multi-series graphing using statically linked Qwt 6.3.0.
- Reproducible portable Windows packaging with Qt runtime deployment, bundled
  MIBs/PIBs, documentation, and third-party notices.

## Known RC limitations

- Controlled acceptance against authorized real devices is still required.
- Packages are unsigned; macOS packages are not notarized and may require the
  tester to approve execution through operating-system security controls.
- Community and legacy USM credential storage has the at-rest limitations
  documented in `security.md`; OS keychain integration is deferred.
- Translations are incomplete and accessibility review is not complete.
- Native installers and a MIB downloader/library are deferred.

Report the exact archive name, operating system, and test result when filing an
RC issue. Use `manual-real-device-acceptance.md` for controlled device testing.
