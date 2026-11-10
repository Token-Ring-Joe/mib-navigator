# Bundled dependency inventory

Engineering checkpoint: 2026-11-07. Version claims below distinguish the current
Linux toolchain from historical Windows artifacts. A previous package does not
establish the contents or validation status of a new platform package.

| Component | Source/toolchain version | Repository material | Current Linux archive |
|---|---|---|---|
| MIB Navigator, derived from SnmpB | 1.0.0-rc1 | GPL-2.0-or-later; `license.txt`, source notices and `NOTICE.md` | executable and GPL text |
| Qt | VM 6.11.2; project minimum 6.10; CI configured for 6.10.3 | external system/SDK Qt; module licensing depends on the selected distribution | dynamically linked Core, Gui, Widgets, Network and Svg; system runtime/plugins required |
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
