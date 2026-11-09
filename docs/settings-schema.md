# SnmpB settings schema

SnmpB uses a user-scope `QSettings` INI file with organization domain
`snmpb.sourceforge.net`, application name `SnmpB`, and INI format. On Windows
this resolves beneath the configured user-scope INI path as
`snmpb.sourceforge.net/SnmpB.ini`.

| Key | Type | Read default | Owner/use |
|---|---|---:|---|
| `network/enableipv4` | bool | `true` | IPv4 transport enablement |
| `network/trapport4` | int | `162` | IPv4 trap listener port |
| `network/enableipv6` | bool | `true` | IPv6 transport enablement |
| `network/trapport6` | int | `162` | IPv6 trap listener port |
| `ui/horizontalsplit` | bool | `false` | Query splitter orientation |
| `ui/expandtrapbinding` | bool | `true` | Expanded trap varbind display |
| `ui/selectedprofile` | string | `localhost` | Last selected Agent Profile |
| `ui/selectedprofileid` | UUID string | empty | Stable ID of the last selected Agent Profile; preferred over the legacy name |
| `ui/selectedproto` | int | `0` | Last selected SNMP protocol |
| `misc/showagentname` | bool | `false` | Show Agent Profile name in trap display |
| `misc/automaticloading` | int | `2` | MIB dependency loading: 1 load, 2 prompt, 3 disable |
| `discovery/destinationfolderid` | stable folder ID string | empty | Optional Device Manager destination for newly created Discovery profiles; missing IDs fall back to Unfiled |
| `mainwindow/size` | QSize | absent | Main-window size, restored only when present |
| `mainwindow/pos` | QPoint | absent | Main-window position, restored only when present |
| `mibpaths/size` | int | `0` | QSettings array length; zero triggers executable-relative defaults |
| `mibpaths/<index>/dir` | string | empty | MIB/PIB search path array entry |
| `mibpreloads/size` | int | `0` | QSettings array length; zero triggers built-in preload defaults |
| `mibpreloads/<index>/mib` | string | empty | Automatically wanted MIB module array entry |
| `mib-library/root` | absolute directory | Qt `DocumentsLocation/MIB Navigator/MIBs` | User-visible working MIB collection root |
| `mib-library/migration-v1-complete` | bool | `false` | Successful legacy managed-MIB copy migration marker |

The configured root reserves `Standards` and `Unassigned` as recursive global
library trees that never create Automatic profiles. Other content may use
`Vendor/Product`; each immediate product directory creates one Automatic
profile. Custom profile definitions remain application-owned JSON state.

The built-in preload list is `IF-MIB`, `RFC1213-MIB`, `SNMP-FRAMEWORK-MIB`,
`SNMP-NOTIFICATION-MIB`, `SNMPv2-MIB`, `SNMPv2-TM`, and
`SNMP-VIEW-BASED-ACM-MIB`. Default MIB paths are the staged `mibs` and `pibs`
directories relative to the executable (with platform install-layout handling).

The application also derives sibling files (`smi.conf`, `boot_counter.conf`,
`usm_users.conf`, `agents.conf`, `device-tree.conf`, `profile-metadata.conf`,
`credential-identities.conf`, `community-credentials.conf`,
`credential-bindings.conf`,
`log.conf`, and `graphs.conf`) from the INI
file's directory. Those files have independent schemas and are not Preferences
keys.
