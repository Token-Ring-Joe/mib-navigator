# MIB Navigator 1.0.0-rc1 release notes

Current redesign checkpoint: 2026-11-07. The application is in final usability
and release validation. The RC label does not imply that the current uncommitted
source has completed Windows/macOS CI or owner desktop acceptance.

## Highlights

- Qt 6 Widgets and CMake with independent patched libsmi, SNMP++ and crypto
  targets; tolerant real-world vendor MIB parsing remains a core requirement.
- Independent Target, reusable Credential and MIB Profile controls. Target
  history remembers the last credential without becoming a saved-device store.
- User-visible configurable MIB collection, global Library/provider/dependency
  knowledge, folder-derived Automatic Profiles and editable Custom Profiles.
- Immutable Profile Environments, actionable MIB Problems, collection-wide
  search, product-profile detection and asynchronous Editor verification.
- SNMPv1/v2c/v3 operations, responsive Walk/Stop, sparse-aware table grids,
  decoded indexes, exact row selection and CSV export.
- Watch rows retain their own target/credential; raw/interpreted values and
  mismatch warnings are shared by Query, Tables, Watch and Traps.
- Typed trap history with active-Profile reinterpretation and Copy source IP;
  request logging with plain-language SNMPv3 refusal explanations.
- Query/MIBs/Traps workspaces, conditional MIB-only Problems, a shared Operations
  panel, Light/Dark/Follow System themes and consistent smaller-window dialogs.
- Discoverable keyboard navigation, usable work-area focus, empty/busy guidance,
  filter recovery messages and safer restored window/splitter geometry.

Graphs/Qwt, Discovery and saved-device/Device Manager features are retired.
Existing `agents.conf` is left untouched and is not migrated. Query Macros has
an agreed design but is not implemented; the temporary Multiple Varbinds
workflow remains until a tested replacement is accepted.

## Known RC limitations

- Owner desktop review and current Windows/macOS CI remain release gates.
  Native packages are unsigned; macOS notarization is not provided.
- Watch definitions are not saved across restarts; table/column Watch expansion
  remains deferred. Query Macros has an agreed design and awaits implementation.
- A global SNMPv3 context-name preference is planned but absent; connection-bar
  requests currently use the default empty context.
- IANA downloads are explicit. Comparable revision metadata is unavailable,
  so automatic Check for Updates is not implemented.
- Broader DISPLAY-HINT/TEXTUAL-CONVENTION checks and further Problems
  filtering/sorting/copy/export remain refinements.
- Existing application/libsmi leaks and Qt deprecation/unchecked-return warnings
  remain. Broad ASAN invalid-access checks do not imply a leak-free application.
  Shutdown can wait for an uninterruptible parser operation.
- Credential storage has the at-rest limitations documented in `security.md`;
  OS keychain integration remains deferred. Translations remain incomplete.

See `release-candidate-validation.md` for build/package checks and
`manual-real-device-acceptance.md` for explicitly authorized lab acceptance.
The development-tree record `docs/finish-review-validation.md` contains current
Linux evidence and the exact manual-validation artifact. Report the executable's
About-box build identity, operating system and observed behavior with an RC issue.
