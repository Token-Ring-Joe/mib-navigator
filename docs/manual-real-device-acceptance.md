# Manual real-device acceptance checklist

Current redesign: 2026-11-07. Perform only the applicable checks on devices
explicitly authorized for this acceptance session. This checklist grants no
network authorization. Record build identity from About, OS, device/firmware,
Target, credential display name and MIB Profile; never record credential secrets.
Use a deliberately safe writable object for any specifically authorized SET test.

## Connection and SNMP

- [ ] Type a Target and choose a reusable Credential and MIB Profile independently.
- [ ] SNMPv1/v2c GET, GETNEXT, GETBULK where applicable, and Walk return expected
      values/types; Walk ends at its boundary and Stop remains responsive.
- [ ] Sparse table grids include rows with missing cells; decoded string/IP,
      IMPLIED and AUGMENTS indexes match the device's data where available.
- [ ] Query Table > Select Instance retrieves exactly that row; Cancel preserves
      previous results and does not start retrieval.
- [ ] Applicable SNMPv3 security levels work; rejected credentials receive a clear
      explanation. Current requests use the default empty SNMPv3 context.
- [ ] A separately authorized safe single-object SET succeeds and reread confirms it.
- [ ] Results, Output and Log agree on completion/failure. CSV includes the
      expected rows, columns and missing cells.

## Watch and profile detection

- [ ] Start Watch from the Tree or Results; inspect value/change/rate and raw data.
- [ ] Change the connection bar; the original Watch row keeps its captured Target
      and Credential reference. Pause, Resume and Remove work.
- [ ] Counter wrap/restart does not create a false rate spike, if a controlled
      fixture is available. Watches are not restored after application restart.
- [ ] Click Detect only against the authorized Target; the proposed Profile is
      appropriate. A timeout/no match leaves the selection unchanged.

## MIBs and notifications

- [ ] Refresh product MIB folders; Automatic Profiles follow the collection.
      Inspecting a different profile in Profiles does not activate it in Query.
- [ ] Open and Verify a vendor MIB; diagnostics identify the source/line and the
      application remains responsive. Problems are visible only inside MIBs.
- [ ] Receive authorized v1/v2c/v3 notifications where a sender is available.
      Raw/interpreted values, warnings and Copy source IP are correct.
- [ ] Changing the active MIB Profile reinterprets retained trap history without
      changing the selected record. Clear History and the history bound work.

## Desktop and lifecycle

- [ ] Preferences, Credentials and Editor Find/Replace/Go to Line remain usable
      when resized. Keyboard Navigation reaches usable content controls.
- [ ] Restart restores window position, Query pane sizes, Inspector visibility and
      Operations panel state. A changed monitor layout leaves the window accessible.
- [ ] Close during an operation/verification and restart normally. Parser shutdown
      may wait for a running parse. The trap listener can bind again.
- [ ] User configuration/credentials stay outside the install tree.

Graphs, Discovery and saved-device management are retired and are not acceptance
workflows. Query Macros and Watch-all/table expansion are not implemented.
