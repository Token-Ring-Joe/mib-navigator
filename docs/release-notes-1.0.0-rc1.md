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
- Global SNMPv3 context name with captured operation semantics and UTF-8 PDU data.
- Watch definitions save automatically, return paused after restart, and support
  per-row interval edits while retaining target/port/credential/context.
- Watch raw/interpreted values and
  mismatch warnings are shared by Query, Tables, Watch and Traps.
- Typed trap history with active-Profile reinterpretation and Copy source IP;
  request logging with plain-language SNMPv3 refusal explanations.
- Query/MIBs/Traps workspaces, conditional MIB-only Problems, a shared Operations
  panel, Light/Dark/Follow System themes and consistent smaller-window dialogs.
- MIB Problems text/severity filtering, severity sorting and CSV copy/export
  with full diagnostics/source paths; shared timestamped library logging.
- Discoverable keyboard navigation, usable work-area focus, empty/busy guidance,
  filter recovery messages and safer restored window/splitter geometry.

Graphs/Qwt, Discovery and saved-device/Device Manager features are retired.
Existing `agents.conf` is left untouched and is not migrated. Query Macros milestone 1 provides offline definition editing and storage;
execution remains later work. See [the milestone record](query-macros-milestone-1.md); the temporary Multiple Varbinds
workflow remains until a tested replacement is accepted.

The maintenance follow-up adds shared integer/octet DISPLAY-HINT presentation,
ASCII/UTF-8 and standard textual-convention warnings, complete Counter64 range
checking and undefined-BITS warnings. Received raw data remains available and
unsupported hints retain received presentation. SET range/SIZE alternatives now
accept any permitted alternative. Narrow diagnostic-buffer/UI ownership fixes
reduce measured leaks; historical scanner/pending-node leaks remain. See
[maintenance validation](maintenance-five-validation.md).

## Known RC limitations

- Owner desktop review and current Windows/macOS CI remain release gates.
  Native packages are unsigned; macOS notarization is not provided.
- Table/column Watch expansion remains deferred. Query Macros definitions can be edited
  offline; captured execution and grouped Results remain later milestones. Saved watches require explicit Resume after
  restart; no saved values or counter baselines are restored.
- IANA downloads are explicit. Comparable revision metadata is unavailable,
  so automatic Check for Updates is not implemented.
- Broader DISPLAY-HINT/TEXTUAL-CONVENTION checks remain refinements. Watch polls
  and full PDU decoding remain outside the request log.
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
