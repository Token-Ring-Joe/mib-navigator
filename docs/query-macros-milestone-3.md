# Query Macros v1 — milestone 3 (table groups)

Implemented 2026-11-10 on the owner's instruction ("Lets proceed with M3").
Design: [query-macros-v1-design.md](query-macros-v1-design.md).

## What a user can do now

- A macro's **table group** runs: the whole table (**All instances**), only the
  rows listed under **Fixed instances**, or rows picked when the macro runs
  (**Choose when run**).
- Each table group is its own entry in the Results group selector and shows one
  grid with a column per selected object, decoded index text when the MIB Profile
  defines the INDEX, and a marked cell where a sparse row has no value.
- Scalar and exact-instance entries still run first, as before. Tables follow, in
  the order they appear in the macro, one at a time.
- Stop keeps every row already received, marks unread tables "Not read", and sends
  nothing further. A refused login (SNMPv3 REPORT, authorization error) ends the run
  without repeating the same login for every remaining table.
- A table that times out, has no instances, or whose Profile definition no longer
  matches is explained on its own group; other groups still run.

## Choose when run

Instance discovery runs first, inside the same foreground operation and with the
captured Target, Credential and MIB Profile. The picker lists each instance with its
decoded index. **Cancel (or Stop) leaves the previous Results exactly as they were**
and nothing is retrieved; results are replaced only after every picker is answered.

## Rules kept from the design

- Numeric OIDs are the identity. A column or row the captured Profile does not
  define is read as raw numeric data; one the Profile defines as something else
  is reported for review and skipped.
- Fixed instances are exact reads. A vanished row is reported ("Instance 7: …")
  and never replaced by its successor.
- SNMPv1 uses GETNEXT only; v2c/v3 use GETBULK with the existing tooBig handling.
- Every request is logged with the macro and group name; no secrets.

## Not in this milestone

- Entries for different rows that provably share an index (AUGMENTS) are shown as
  separate groups. The design allows merging them; separate groups are always
  correct, so merging is deferred.
- The picker dialog is modal and is not covered by an automated UI test (the
  runner's chooser callback is). Everything around it is.
- Legacy Multiple Varbinds retirement is milestone 4.

## Evidence

`snmpb.query-macro-runner` drives the runner against a scripted `FakeSnmpAgent`:
all/sparse rows, SNMPv1, fixed with a vanished row, choose-when-run ordering,
cancelled picker, Stop during discovery and during retrieval, empty table,
refusal, timeout and independent tables. `snmpb.query-results` covers the table
grid, group switching and CSV.

The shared `SnmpTableAsyncRunner` and `SnmpInstanceAsyncRunner` now accept a new
request as soon as the previous result was delivered (the pattern the single-read
runner already used); previously a second request could be refused while the first
thread was still exiting.

## Owner feedback, 2026-11-11

The owner reported three problems while testing:

1. A table could not be added to an existing macro (it worked in a new macro).
   They used the right-click command in the MIB tree. Nothing changed and no
   message appeared.
2. After adding a non-table object to that macro, only the non-table was shown.
3. With several tables in one macro, only one table was ever shown.

### What was found

The first problem could **not** be reproduced from the code. The panel's tests
already add a table to a macro that holds other entries, through the real
object picker and the real tree command. The likely cause is silent feedback:
adding a table the macro already holds (same row, same columns) merged into the
existing entry with no change and no message, which looks exactly like a failed
add. This is a hypothesis, not a confirmed cause. If it happens again with a
table that is *not* already in the macro, please report the macro's entries and
the status line.

Problems 2 and 3 were a display issue. A macro run makes one result group for
the objects and values and one more for each table, but Results showed only one
group at a time through a small drop-down that was easy to miss.

### What changed

- Adding an object to a macro always says what happened in the macro's status
  line: "Added … as a new entry", "Added N column(s) to the existing … table
  group", or "… is already in … with those columns. Nothing was added."
  Repeating an identical add no longer marks the macro as changed.
- Results shows one **tab per group** (objects and values, then each table)
  whenever a macro run has more than one group. The old drop-down is kept
  hidden as the list the tabs mirror.

Tests: `snmpb.query-macros-panel` (new messages) and `snmpb.query-results`
(tabs follow the group list, clicking a tab shows that group, ordinary queries
show no tabs). Awaiting owner review.
