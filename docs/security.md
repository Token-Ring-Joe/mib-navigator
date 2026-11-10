# Credential storage security notes

Current architecture: 2026-11-07. See [settings-schema.md](settings-schema.md)
and source-tree `docs/redesign-plan.md` for current state ownership.

Target, Credential and MIB Profile are independent controls. Credentials are
managed under Settings > Credentials and reused by reference. There is no saved
Agent Profile/device manager or portable device-profile export. Historical
`agents.conf` files are left untouched and are not loaded or migrated.

## Storage at rest

Preferences retain the historical `snmpb.sourceforge.net/SnmpB` INI identity.
Reusable communities are stored in `community-credentials.conf` in a
plaintext-equivalent form. Canonical SNMPv3 records are stored atomically in
`credential-identities.conf.v2.json`; authentication/privacy secret bytes are
Base64 encoded, **not encrypted**. This canonical store supports independent
credential IDs and several variants of the same wire username. Historical
`usm_users.conf` uses reversible encoding and remains migration input; it is
not the authoritative modern credential editor store.

Canonical SNMPv3 files use owner read/write permissions. File permissions do not
provide encryption or protection from another process running as the same user.
No Windows Credential Manager, macOS Keychain or Linux Secret Service integration
is implemented. OS account permissions and backup protection remain relevant.

## Runtime and exports

Requests capture resolved settings and credentials when dispatched. Watches
persist target/port, credential reference, numeric instance, context and interval;
they store no secrets, results or counter baselines and restore paused. Credential
edits affect subsequent Watch polls. See [settings-schema.md](settings-schema.md).

Structured diagnostics redact known secret fields and wrap raw SNMP++/libsmi
messages before shared file/UI delivery. Request-log exports contain operation
metadata rather than secret values. Problems CSV contains MIB diagnostics and
full source paths. Review logs and exports before sharing: endpoint names, source
paths and vendor-supplied text can still identify a lab or customer.

Release-tree checks reject private settings, credentials, Watch stores/locks,
logs and parser state. They do not turn a development directory into a safe
package automatically; use the explicit CMake install and archive-verification
workflow. Do not attach user configuration files to issue reports.
