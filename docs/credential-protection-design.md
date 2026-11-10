# Credential protection at rest — design and status

Owner direction (2026-11-10): prefer a **master password** over an OS keychain
dependency. Outstanding task 13.

## Done: the vault (no behavior change yet)

`CredentialVault` ([credentialvault.h](../app/credentialvault.h)) stores a flat map of
`<credentialId>/<field>` to secret bytes in one file.

- PBKDF2-HMAC-SHA256 (600,000 rounds, random 16-byte salt) from the master password.
- AES-256-CBC with a random IV, then HMAC-SHA256 over format, rounds, salt, IV and
  ciphertext (encrypt-then-MAC). A wrong password or any modified byte fails the MAC
  before decryption; a failed unlock never touches the file.
- Only Qt (Core, Network) and the already-bundled libtomcrypt AES are used. No new
  dependency, no libtomcrypt build change.
- Tests (`snmpb.credential-vault`): round trip of binary secrets, no secret visible in
  the file in plain or base64 form, wrong password, tampering of each field, garbage and
  newer-format files, store/lock, password change, removal, unwritable location.

The master password is never stored. Lost password means lost secrets; the non-secret
details (IDs, names, users, protocols) stay in the existing files so secrets can be
re-entered and every credential ID is preserved.

## Not done: wiring it in

Protection is opt-in and off by default, so existing installs and the SnmpB-compatible
layout are unchanged until the user turns it on. Turning it on means secrets leave
three places:

1. `community-credentials.conf` (Qt INI): `readCommunity` / `writeCommunity`.
2. `usm_users.conf.v2.json`: base64 `authSecret` / `privacySecret`.
3. `usm_users.conf` (SNMP++ format, hex passwords): **loaded straight into the SNMP
   engine at startup** (`Agent::Init`, `usm->load_users`) and rewritten by
   `UsmCredentialRuntimeRepository::replaceAndSave` on every v3 edit.

Plan, each step with its own tests:

1. Repositories take an optional vault. Saves write secrets to the vault first, then the
   file with blank secret fields and a `protected` marker. A protected file with no vault
   is an error, not a silent blank. A locked vault refuses saves instead of blanking.
2. Enable / disable / change password as one coordinator with rollback: vault first,
   then the three files; any failure restores the files and removes the new vault.
3. Startup: if the vault exists, ask for the password before the services load; on
   cancel the app runs with credentials locked (names visible, queries needing a secret
   say "Unlock credentials" instead of sending a blank one). Unlock reloads both services
   and pushes the SNMPv3 users into the engine.
4. Credentials screen: Protect…, Change password…, Remove protection…, Unlock.

## Decision needed from the owner

For SNMPv3 the engine reads `usm_users.conf` directly. Two ways to keep passwords out
of it while protection is on:

- **A (recommended):** while protected, do not write passwords to `usm_users.conf` and do
  not load it at startup; fill the engine's user table from the unlocked vault. The file
  is left untouched on disk when protection is turned on (never deleted), and written back
  with secrets if protection is turned off. A third-party tool or an older SnmpB that
  reads that file would not see the passwords while protection is on.
- **B:** keep writing `usm_users.conf` as today. Simpler, but the SNMPv3 passwords stay
  readable on disk and protection would only cover communities. Not recommended.

Until this is confirmed, nothing reads or writes the vault outside its tests.
