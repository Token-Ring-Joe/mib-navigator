# MIB Navigator

MIB Navigator 1.0.0-rc1 is a free, cross-platform Qt 6 Widgets application for
SNMP and MIB engineering. It combines a tolerant MIB Library and Profiles with
SNMPv1/v2c/v3 queries, sparse-aware tables, value watching, notification inspection,
and request logging.

A session has three independent choices: **Target**, reusable **Credential**, and
**MIB Profile**. The configurable, user-visible MIB collection supplies Automatic
product profiles and manually composed Custom profiles. Query, MIBs and Traps are
permanent workspaces. Graphs, Discovery and saved-device management are retired.

The project is in final usability and release validation. See the authoritative
[redesign plan](docs/redesign-plan.md),
[latest finishing validation](docs/finish-review-validation.md),
[current outstanding tasks](docs/outstanding-tasks.md), and
[agreed Query Macros design](docs/query-macros-v1-design.md). Query Macros is
design work; it is not implemented.

Builds use CMake 3.22+, a C++17 compiler and Qt 6.10+ (Core, Widgets, Network and
Svg). Bundled patched libsmi, SNMP++ and crypto code remain independent targets.
See [build and release validation](docs/release-candidate-validation.md) for the
configure/build/test/install workflow.

It is a substantially modified continuation of the GPL-licensed
[SnmpB](https://sourceforge.net/projects/snmpb/) project, not an official SnmpB
release. See [NOTICE.md](NOTICE.md), [license.txt](license.txt), and
[the dependency inventory](docs/third-party-dependencies.md) for attribution
and licensing details.

Project source: https://github.com/transientuser/snmpb-next
