# Stable Agent Profile identity

Each `AgentProfileRecord` has an opaque UUID-form `profileId`. The ID is
independent of the display name, endpoint, and all SNMP settings. Rename and
folder movement preserve it; Duplicate creates a new one.

## agents.conf migration

The existing QSettings `agents` array adds one field:

```text
agents/N/id=<UUID without braces>
```

Legacy arrays without `id` still load unchanged. Each record receives a unique
in-memory ID, including records with duplicate display names. Loading alone
does not rewrite the file. IDs persist with the next legitimate profile or
device-organization save. Invalid or repeated persisted IDs are regenerated
in memory without merging records.

## Selection and display names

Primary Query and Device Manager selection resolves by `profileId`. The legacy
name resolver remains for old UI/persistence boundaries. A legacy selected
name migrates only when unique; the stable selection is stored in
`ui/selectedprofileid`, while `ui/selectedprofile` remains as a readable
compatibility fallback.

Agent Profile names remain labels. Duplicate legacy names remain separate
records and are independently selectable through ID-backed combo-box item
data and Device Manager nodes.

## Compatibility boundaries

- Discovery retains its independent selector but stores profile IDs in combo
  item data and resolves its selected template by ID.
- Graphing remains disabled under Qt 6. Its `graphs.conf` records still store
  `agent=<profile name>`. Migrating that reference is deferred until Graph/Qwt
  can be compiled and regression-tested without restoring Qwt in this work.
- `GetAgentProfile(name)`, `GetAgentsList()`, `SelectProfileByName()`, and the
  name-based `AgentSelectionResolver::Resolve()` remain explicit legacy or
  display adapters. Primary request execution uses `ResolveById()`.
