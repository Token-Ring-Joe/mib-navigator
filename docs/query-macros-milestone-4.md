# Query Macros v1 — milestone 4 (retirement and release validation)

Implemented 2026-11-11 on the owner's instruction, after the owner accepted
milestone 3 ("It works").

## What changed

The old **Multiple Varbinds** dialog is retired. It was the temporary way to
build a list of objects and run Get, Get Next, Get Bulk or Set on all of them.
Query Macros now cover saved multi-object reads, so the following are removed:

- the dialog (`varbinds.ui`) and the `Agent::Varbinds*` code behind it;
- the **SNMP** menu and its only command, "Multiple Varbinds…" (Ctrl+M): the
  menu bar is now File, Edit, View, MIB, Settings, Help;
- the "Multiple Varbinds…" entries in the Tree's right-click menu and in the
  legacy `MibView` context menu.

Kept: **single-object Set** (Tree right-click > Set…), which uses its own path
(`Agent::SetVarbinds`), and every other request path.

## What is no longer possible

- Running **Get Next** or **Get Bulk** on an arbitrary hand-built list of
  objects. Macros read scalars, exact instances and whole or partial tables;
  they do not do Get Next or Get Bulk on a list.
- Setting several objects in one request (outside the agreed v1 scope).

## Evidence

Debug build with the dialog removed, the full deterministic suite
(`snmpb.launch-smoke` now asserts the six-menu order), and the Release suite
with the tolerance scenarios. See the final report in the task record for the
exact counts.
