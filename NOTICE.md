# MIB Navigator modification notice

MIB Navigator is a substantially modified derivative and continuation of the
GPL-licensed [SnmpB project](https://sourceforge.net/projects/snmpb/). The
original SnmpB author is Martin Jolicoeur; later upstream maintenance included
Max ulidtko. MIB Navigator modifications and continued development are by
Joseph Wood and contributors.

MIB Navigator is not an official release of SnmpB, and no endorsement by the
original authors or project is implied. Original SnmpB source is available from
the [SourceForge repository](https://sourceforge.net/p/snmpb/code/ci/master/tree/).
MIB Navigator source is at
[github.com/Token-Ring-Joe/mib-navigator](https://github.com/Token-Ring-Joe/mib-navigator).

The application is distributed under GPL-2.0-or-later as established by
`license.txt` and applicable source notices. Bundled third-party components
retain their own copyrights and licenses; see `docs/third-party-dependencies.md`
and the installed license directory.

## Qt

MIB Navigator uses the Qt 6 toolkit (Core, Gui, Widgets, Network and Svg) under the
GNU Lesser General Public License version 3 (LGPL-3.0), which builds on the GNU General
Public License version 3. The texts are in `share/snmpb/licenses/Qt/LGPL-3.0.txt` and
`GPL-3.0.txt` (inside the application folder on macOS).

- **How it is linked.** Qt is linked dynamically and is not modified. On Windows the Qt
  DLLs are in `bin/` and its plugins in `plugins/`; on macOS the Qt frameworks are inside
  `MIB Navigator.app/Contents/Frameworks`. You may replace them with another build of the
  same Qt version. On Linux the program uses your system's Qt and bundles none.
- **Which version.** The Windows and macOS archives list their Qt version and its parts
  in `share/snmpb/licenses/Qt/*.spdx`.
- **Source code.** Qt's source for every version is available from
  <https://download.qt.io/official_releases/qt/> and <https://code.qt.io/>. If you
  cannot obtain it there, ask in the project's Issues and the maintainers will provide
  it.
- Qt is a trademark of The Qt Company Ltd.

Other components shipped in the Windows archive are listed in
`third-party-dependencies.md`.
