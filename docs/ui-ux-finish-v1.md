# UI / UX Finish v1

UI / UX Finish v1 preserves the established Qt Widgets tab and dock structure.
It consolidates modernized services into the active presentation rather than
introducing a new command framework or persistence format.

The Graph editor is transactional. A dialog edits a working `GraphDefinition`
including name, polling interval, history size, stable Agent Profile identity,
protocol, canonical numeric OID, label, color, width, and pen style. Cancel
does not call `GraphService`; acceptance validates and sends one definition to
the service. The Live page remains Qwt presentation over value-based sampling.

The main MIB browser is now a `QTreeView` over `MibTreeFilterModel` and
`MibTreeModel`. Canonical numeric OID is selection identity and is retained
across compatible model resets. Details are rendered from snapshot roles, so
the visible browser owns no `SmiNode` or `QTreeWidgetItem` semantic state.
`BasicMibView`, `MibNode`, and the old projection builder remain compiled only
for compatibility dialogs and should be removed in a later isolated milestone.

Editor verification diagnostics are presented through `MibDiagnosticModel`
with severity, source, line, and original message columns. Severity filtering,
sorting, and double-click line navigation operate on structured roles. Patched
libsmi messages and severity values remain unchanged.

Startup MIB inventory intentionally retains historical filename rules. In
particular, readable `.txt` files are candidates because net-snmp distributes
valid MIBs with that suffix; extensionless and several vendor suffixes are also
supported. Consequently, unrelated text files placed in a configured MIB
search directory may be attempted. Failed candidates now display their full
path and identify that the directory came from configured MIB search paths.
Parser behavior and candidate semantics are unchanged.

Additional consistency work includes a visible trap-history clear action,
Devices toolbar distinction between New Profile and New Folder, clearer
Discovery Start/Stop state, resizable discovery address fields, bounded-setting
tooltips, and updated About information using the Qt runtime version. Secrets
are never included in status or diagnostic presentation.

Release validation still requires native Linux and macOS builds, packaging or
installer production, controlled real endpoint testing, translation completion,
and a separate secure credential-storage decision. Those are intentionally not
part of this UI consolidation milestone.
