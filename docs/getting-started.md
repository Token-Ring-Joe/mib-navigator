# Getting started with MIB Navigator

MIB Navigator is a tool for browsing MIBs and querying SNMP devices in a lab or
while engineering MIBs. It is not a network management system: there is no device
inventory, discovery, alerting or scheduled polling.

This guide covers a first session. Release status and known limits are at the end.

## 1. Start it

Unpack the archive for your platform and run:

| Platform | Run |
|---|---|
| Windows | `bin/mib-navigator.exe` |
| Linux | `bin/mib-navigator` (needs Qt 6.10 or newer and the usual system libraries) |
| macOS | `MIB Navigator.app` |

The builds are unsigned, so Windows SmartScreen and macOS Gatekeeper warn on the
first launch. On Windows choose **More info**, then **Run anyway**. On macOS
right-click the app and choose **Open**.

## 2. Add your MIBs

MIB Navigator reads MIB files from one visible folder, by default
`Documents/MIB Navigator/MIBs/`. To find it, open the **MIBs** tab, then
**Library**, and click **Open MIB Folder**. The folder can be changed in
**Settings > Preferences**.

Arrange it like this:

```text
MIBs/
    Standards/            IETF, IANA, IEEE ...   (shared by every profile)
    Unassigned/                                  (kept, but belongs to no product)
    <Vendor>/
        <Product>/        every folder here becomes an Automatic profile
```

For example, copy an Extreme Networks download into `MIBs/Extreme Networks/Fabric Engine/`
and an **Extreme Networks / Fabric Engine** profile appears. You do not create it.
Vendor and product names are not built in: any `Vendor/Product` folder works.

Your files are never modified. The index and caches live in the application data
folder, not in your MIB folder.

Some vendors ship helper files with MIB extensions (`.my`, `.mib`) that are not MIBs
for loading. If **Problems** reports an odd parse error from a file such as that,
move the file out of the product folder.

## 3. Pick a MIB profile

A **profile** is the set of MIBs used to interpret results. The Tree shows only
the selected profile.

- **Automatic** profiles come from your product folders. You cannot edit them
  member by member.
- **Custom** profiles are yours to edit: for example "lab", "customer X", or a
  troubleshooting subset.
- **Standards / MIB-II** is the default. There is no "all MIBs" profile, because
  loading everything is rarely what you want. Build a Custom profile if you do.

Choose one in the **MIB Profile** box at the top of the window. After you enter a
target and credential, **Detect** reads the device's `sysObjectID` and suggests the
profile whose MIBs describe that product. It contacts the device only when you
click it.

## 4. Set up a credential

Open **Settings > Credentials** and click **New**. Choose SNMPv1, SNMPv2c or
SNMPv3, then fill in the community string or the SNMPv3 user and passphrases.
Saved credentials appear in the **Credential** box at the top of the window and are
reused for any target.

To keep secrets encrypted on disk, use **Password protection** in the same window.
It is off by default.

## 5. Query a device

At the top of the window:

1. **Target**: type an IP address or host name. Recent targets are remembered.
2. **Credential**: pick the one to use.
3. **MIB Profile**: pick the MIBs that should interpret the answer.

Only query devices you are authorised to query. Port, timeout and retries are in
**Settings > Preferences**.

On the **Query** tab, select an object in the Tree and use the **SNMP** menu or
right-click: Get, Get Next, Get Bulk, Walk, Table and Set. Results appear in the
**Results** tab beside the Tree.

- The search box above the Tree shows the selected object's OID. Click in it and type
  a name or a numeric OID to search every MIB in your collection.
- **Results** shows the module, object and instance, the value and the received
  type. Right-click a column heading to choose which columns appear. Warnings
  mark values that do not match what the MIB allows.
- Right-click a value for **Watch** to poll it at an interval, with the change and
  rate since the last poll.
- **Macros** saves a query (scalars, specific instances or whole tables) so you can
  run it again.
- If an SNMPv3 request fails, the message explains the likely cause: wrong user,
  authentication, privacy passphrase, security level or context.

## 6. Check your MIBs

Open the **MIBs** tab.

- **Library** lists every module in your collection with its files, dependencies and
  conflicts, and flags missing imports and different copies of the same module.
- **Profiles** shows what each profile contains.
- **Editor** opens a MIB with syntax colouring and line numbers, and **Verify** checks
  it with the same tolerant parser the Tree uses.
- **Problems** (in the bottom panel while on the MIBs tab) lists what the parser or the
  Library found, with what it affects and what to try.

## 7. Traps

The **Traps** tab receives SNMP traps and informs and decodes them with the
selected MIB profile. For a mix of vendors, make a Custom profile that holds
the MIBs of all of them. Receiving traps may need elevated rights or a port
above 1024, depending on your system.

## Release status and known limits

This is a release candidate (1.0.0-rc1).

- Windows and macOS are built and tested by automation; real-machine testing is
  limited so far, and the first Windows tests were basic functionality.
- The builds are unsigned.
- Several features are intentionally missing: device inventory, discovery, graphs
  and saved-walk browsing were retired from SnmpB.
- Translations are not complete; the interface is English.

Project page: <https://github.com/Token-Ring-Joe/mib-navigator>. To report a
problem, open an issue on the project page and include your platform, the version from **Help > About**, and the exact
message. Do not share credentials or confidential MIBs.
