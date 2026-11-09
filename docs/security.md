# Credential storage security notes

MIB Navigator 1.0.0-rc1 stores runtime configuration in the current user's
configuration directory. Access to that directory should be restricted to the
user account running SnmpB.

- SNMPv1/v2c read and write community values in agent profiles are stored in a
  plaintext-equivalent configuration form.
- Reusable community credentials are also stored in a plaintext-equivalent
  configuration form.
- SNMPv3 USM passwords managed through the legacy SNMP++ store are encoded but
  are **not cryptographically encrypted** at rest.
- This release does not integrate with Windows Credential Manager, macOS
  Keychain, or Linux Secret Service.

Portable profile export intentionally excludes credential secrets. Do not
publish or attach user configuration files to issue reports, and never include
real community values or USM passwords in diagnostic output.
