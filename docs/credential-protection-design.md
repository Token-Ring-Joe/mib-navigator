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

## Wired in (2026-11-10)

Owner decision: the default stays as before (secrets in the credential files); the master
password is an optional setting. No OS keychain or third-party manager backend is built.

- `CredentialSecretStore` ([credentialsecretstore.h](../app/credentialsecretstore.h)) is the
  interface the repositories use; `CredentialVault` implements it. A later keychain or
  password-manager backend only has to implement it.
- Keys are `community/<id>/read|write` and `usm/<id>/auth|priv`. Each repository replaces only
  its own prefix.
- With a store attached, a repository writes the vault first and then its file with blank
  secret fields and a `protected` marker. If the file write fails, the vault contents are put
  back. A protected file read while the store is locked, or whose secrets are missing, is an
  error, never a blank credential. A locked store refuses saves.
- `CredentialProtection` ([credentialprotection.h](../app/credentialprotection.h)) turns
  protection on, off, changes the password and unlocks. Turning on creates the vault, rewrites
  both files without secrets, and on failure restores the plain files and removes the new
  vault, or keeps the vault if the plain files cannot be restored (it may hold the only copy).
  Turning off rewrites the plain files first and removes the vault only afterwards.
- Services are given the vault only while it exists (`activeSecretStore()`), so with protection
  off nothing changes. `reload()` on both services re-reads the files after an unlock and
  announces the change, which refreshes the connection bar and the SNMPv3 engine users.
- Startup: if a vault exists, the password is asked before the services load. Cancelling
  starts with the credentials locked (names not listed, no secret loaded or sent); the
  Credentials screen offers Unlock.
- SNMPv3 passwords in `usm_users.conf`: nothing in the application writes that file any more
  (`replaceAndSave` has no callers); it is a legacy import read at startup. With protection on
  it is not read. It is never modified or deleted; the "Turn on" confirmation tells the user
  if the file exists, because an older SnmpB may have left passwords in it as readable text.
- Tests: `snmpb.credential-protection` (full workflow with restart, wrong password,
  tampering of the store, locked saves, password change, turn off, failed-write handling) and
  additions to `snmpb.credentials-dialog`.

Known limits: the previous plaintext may still exist in free disk space, backups or editor
swap files after protecting; a forgotten password cannot be recovered (secrets are re-entered,
IDs and names are kept).
