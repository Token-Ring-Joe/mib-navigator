# MIB Navigator

A free, cross-platform graphical SNMP and MIB engineering tool for lab and MIB
work, built with Qt 6 Widgets and CMake. It is not a network management system.

MIB Navigator keeps what made [SnmpB](https://sourceforge.net/projects/snmpb/)
useful, namely its tolerance of real-world and imperfect vendor MIBs, and adds a
modern interface around it.

> **Status: release candidate.** The project is in final usability and release
> validation. Linux is validated locally; Windows and macOS are built and tested
> by GitHub Actions.

## Download

Release-candidate builds for Windows, macOS and Linux are on the
[Releases page](https://github.com/Token-Ring-Joe/mib-navigator/releases). No
GitHub login is needed to download them.

Bug reports and questions go in
[Issues](https://github.com/Token-Ring-Joe/mib-navigator/issues).

## Installation

Download the archive for your platform from the
[Releases page](https://github.com/Token-Ring-Joe/mib-navigator/releases), unpack
it anywhere, and run the program. There is no installer and nothing is written
outside the folder you unpack, apart from your settings and the application
data folder.

| Platform | Archive |
|---|---|
| Windows (x64) | `MIB-Navigator-<version>-windows-x64.zip` |
| macOS (Apple Silicon, arm64) | `MIB-Navigator-<version>-macos-arm64.tar.gz` |
| Linux (x86_64) | `MIB-Navigator-<version>-linux-x86_64.tar.gz` |

The builds are unsigned, so Windows and macOS warn on first launch. This is
expected.

### Windows

1. Unzip the archive (right-click, **Extract All**).
2. Open the extracted folder, then `bin`, and run `mib-navigator.exe`.
3. If SmartScreen says "Windows protected your PC", choose **More info**, then
   **Run anyway**.

Qt and the other libraries are included.

### macOS

Apple Silicon (M1 or newer) only. Intel Macs are not supported by this build.

1. Unpack the archive by double-clicking it, or in Terminal:
   `tar xzf MIB-Navigator-<version>-macos-arm64.tar.gz`
2. Drag `MIB Navigator.app` to **Applications** (optional).
3. Right-click the app, choose **Open**, then **Open** again in the warning
   dialog. After that it starts normally.

If macOS says the app is damaged, it has been quarantined by the download. Clear
that flag and try again:

```bash
xattr -dr com.apple.quarantine "MIB Navigator.app"
```

Qt is included.

### Linux

```bash
tar xzf MIB-Navigator-<version>-linux-x86_64.tar.gz
cd MIB-Navigator-<version>-linux-x86_64/bin
./mib-navigator
```

Unlike the other platforms, the Linux archive does **not** include Qt. It needs
Qt 6.10 or newer (Core, Widgets, Network, Svg) and the usual desktop libraries
installed on your system. Check what is missing with:

```bash
ldd ./mib-navigator | grep "not found"
```

If your distribution's Qt 6 packages are older than 6.10, install a newer Qt from
the [Qt online installer](https://www.qt.io/download-qt-installer) and point the
program at it, for example
`LD_LIBRARY_PATH=$HOME/Qt/6.10.0/gcc_64/lib ./mib-navigator`, or
[build from source](#building).

After the first start, add your MIBs and pick a profile as described in
[Getting started](docs/getting-started.md).

## What it does

- **Tolerant MIB parsing.** A patched libsmi loads vendor MIBs that stricter
  parsers reject, and reports problems as actionable diagnostics instead of
  failing.
- **MIB Library.** Point it at a folder of MIBs. It indexes every module,
  provider, file and dependency, and flags missing dependencies and conflicting
  copies of the same module.
- **MIB Profiles.** Product folders become **Automatic** profiles
  (for example `Extreme Networks/Fabric Engine/`). You can also build **Custom**
  profiles for a lab, a customer or a troubleshooting subset. The MIB tree shows
  only the selected profile.
- **SNMPv1, v2c and v3 operations.** Get, Get Next, Get Bulk, Set and Walk, with
  a responsive Stop, a global SNMPv3 context name and plain-language explanations
  of SNMPv3 refusals.
- **Tables.** Sparse-aware table grids, decoded indexes, exact row selection and
  CSV export.
- **Query Macros.** Saved, editable query definitions that run captured
  scalar, exact-instance and table requests (all rows, fixed rows, or rows you pick
  when it runs) and show grouped results.
- **Watch.** Poll values at per-row intervals. Each Watch keeps the target,
  port, credential and context it was created with, and starts paused after a
  restart.
- **Traps.** Typed notification history that is reinterpreted against the active
  profile, with Copy source IP.
- **Search and diagnostics.** Collection-wide search by name or OID, a MIB
  Problems view with filtering and CSV export, an asynchronous editor verifier
  and a request log.
- **Light, Dark and Follow System themes.**

## How a session works

Three independent controls sit at the top of the main window:

| Control | Meaning |
|---|---|
| **Target** | A typed IP address or hostname, with recent history |
| **Credential** | A saved, reusable SNMP credential managed on its own screen |
| **MIB Profile** | The MIB environment used to interpret results |

Port, timeout and retries are global settings in the menu. There is no
address book or device inventory.

## MIB collection layout

MIB source files live in a visible folder you choose in **Preferences**
(default `Documents/MIB Navigator/MIBs/`):

```text
MIBs/
    Standards/            IETF, IANA, IEEE ...
    Unassigned/
    <Vendor>/
        <Product>/        becomes an Automatic profile
```

`Standards` and `Unassigned` feed the global Library but do not create product
profiles. Vendor and product names are not hard-coded. Indexes, caches and
settings are stored separately in the application data folder, never in your MIB
folder.

## Building

Requirements: CMake 3.22 or newer, a C++17 compiler, Ninja (recommended) and
Qt 6.10 or newer (Core, Widgets, Network, Svg).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/install
```

The patched libsmi, SNMP++ and crypto code are bundled as independent CMake
targets. See [build and release validation](docs/release-candidate-validation.md)
for the full workflow.

## Documentation

- [Getting started](docs/getting-started.md): a first session, from adding MIBs to
  querying a device
- [Redesign plan](docs/redesign-plan.md): product direction and build order
  (authoritative)
- [Release notes](docs/release-notes.md)
- [Outstanding tasks](docs/outstanding-tasks.md)
- [Query Macros design](docs/query-macros-v1-design.md) and
  [current checkpoint](docs/query-macros-milestone-2.md)
- [MIB architecture](docs/mib-architecture.md) and
  [SnmpB MIB compatibility audit](docs/snmpb-mib-compatibility-audit.md)
- [Third-party dependencies](docs/third-party-dependencies.md)

Graphs, Discovery and saved-device management from SnmpB are retired.

## Origin and license

MIB Navigator is a substantially modified continuation of the GPL-licensed
[SnmpB](https://sourceforge.net/projects/snmpb/) project by Martin Jolicoeur,
with later maintenance by Max ulidtko. It is **not** an official SnmpB release,
and no endorsement by the original authors is implied.

Distributed under GPL-2.0-or-later; see [license.txt](license.txt) and
[NOTICE.md](NOTICE.md). Bundled third-party components keep their own licenses.

Project source: <https://github.com/Token-Ring-Joe/mib-navigator>
