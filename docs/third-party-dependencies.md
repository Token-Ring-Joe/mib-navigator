# Bundled dependency inventory

Engineering checkpoint: 2026-11-07. Version claims below distinguish the current
Linux toolchain from historical Windows artifacts. A previous package does not
establish the contents or validation status of a new platform package.

| Component | Source/toolchain version | Repository material | Current Linux archive |
|---|---|---|---|
| MIB Navigator, derived from SnmpB | current release (Help > About) | GPL-2.0-or-later; `license.txt`, source notices and `NOTICE.md` | executable and GPL text |
| Qt | VM 6.11.2; project minimum 6.10; release archives bundle 6.10.3 (CI) | external system/SDK Qt; module licensing depends on the selected distribution | dynamically linked Core, Gui, Widgets, Network and Svg; system runtime/plugins required |
| libsmi | 0.5.0, locally patched | `libsmi/COPYING` | static application dependency; license included |
| SNMP++ | 3.3.10 | `snmp++/LICENSE` and source notices | static application dependency; notice included |
| LibTomCrypt | 1.17 | `libtomcrypt/LICENSE` | static application dependency; license included |
| Open Iconic | repository-bundled assets | `app/icons/open-iconic/LICENSE.txt` | embedded resources; MIT text included |
| Devicons | repository-bundled assets | `app/icons/devicons/LICENSE.txt` | embedded resources; MIT text included |
| Feather | repository-bundled assets | `app/icons/feathericons/LICENSE.txt` | embedded resources; MIT text included |
| Ionicons | repository-bundled assets | `app/icons/ionicons/LICENSE.txt` | embedded resources; MIT text included |

The Linux TGZ is an install-layout archive, **not a self-contained Linux runtime**.
It uses the VM/system Qt and other shared libraries. A clean extraction is checked
with `ldd`, installed offscreen startup/restart and scripted loopback SNMP. Those
checks establish operation on the validated VM; they do not establish compatibility
with older glibc/libstdc++ versions or every Linux distribution. Exact linked files,
package versions, hashes and packaged license files are retained with the latest
[maintenance validation](maintenance-five-validation.md).

### Binaries bundled in the release archives (verified 2026-11-12 against 1.0.0-rc2)

| Archive | Qt | Bundled |
|---|---|---|
| Windows x64 | 6.10.3 (MSVC 2022), dynamic | Qt6 Core, Gui, Widgets, Network, Svg; plugins for platform (windows, offscreen), styles, TLS (Schannel), network information, SVG icons and image formats (SVG, JPEG, ICO, GIF), generic touch input |
| macOS arm64 | 6.10.3 (Apple clang), dynamic | QtCore, QtDBus, QtGui, QtNetwork, QtSvg, QtWidgets frameworks; plugins for platform (cocoa), styles, TLS, network information, SVG icons and image formats |
| Linux x86_64 | uses system Qt 6.10 or newer | none bundled |

Three further DLLs ship in the Windows archive, copied by Qt's deployment tool. They are
not Qt and not covered by Qt's licence inventories:

| File | What it is | Terms |
|---|---|---|
| `D3Dcompiler_47.dll` | Microsoft Direct3D shader compiler | Microsoft redistributable; not open source |
| `opengl32sw.dll` | Mesa llvmpipe software OpenGL fallback | MIT (Mesa) |
| `icuuc.dll` | ICU common library forwarder stub | ICU / Unicode licence |

Qt itself carries smaller third-party libraries (for example under MIT, BSD-3-Clause and
Zlib terms); they are itemised in the Qt inventories that the Windows and macOS
archives install under `share/snmpb/licenses/Qt/`. The LGPL-3.0 and GPL-3.0 texts install
beside them, and `NOTICE.md` explains the linking and where to get Qt's source.

Windows and macOS deployment uses Qt's native deployment tooling. Each new native
artifact requires its own dependency/plugin inventory and CI/desktop validation.
Historical Windows Qt 6.11.1/Schannel inventory is evidence for that older artifact
only. The CI/local Qt version difference and headless-launch exclusion remain
visible release-review items; this batch does not change the CI toolchain.

Original license material is installed unchanged under `share/snmpb/licenses`.
CMake also stages Qt SDK SPDX inventories when the selected SDK supplies them;
the system Qt installation may not include those inventories. Release owners
still select the Qt distribution route and complete its notices/source-material
review before publication. This inventory does not change linkage or licensing.
