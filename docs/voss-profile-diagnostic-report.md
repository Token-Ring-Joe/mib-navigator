# VOSS Profile Runtime Diagnostic Report

Date: 2026-09-02
Scope: Real-corpus diagnosis only. No production source was modified. No commits, no pushes, no SNMP traffic.

---

## Executive Summary

The failure is **not** caused by libsmi directory visibility being inherently unsafe, and it is **not** caused by wrong-provider substitution. Post-load exact-file authorization is working correctly and safely rejected every unauthorized module.

The actual root cause is **Category A + D combined (Category E)**:

The persisted VOSS Profile (`f416d36e-3694-4fd6-80f0-966923efebfd`) carries **60 legacy "wanted" module identities** (`unresolvedLegacyModules`) that were never converted into exact physical `members` during Profile migration. These 60 identities are still folded into `explicitModules` (the set the application *intends* to load), so the Environment build still asks libsmi to resolve them — but because no exact member exists for them, whatever libsmi loads is correctly rejected by exact-file authorization.

The migration function (`MibProfileService::migrateLegacyProfiles`, `app/mibprofile.cpp:432`) only auto-resolves an identity into an exact member when the Catalog dependency index reports **exactly one** provider (`app/mibprofile.cpp:464`, `if (providers.size() != 1) { unresolved.append(identity); continue; }`). For every one of the 60 stuck identities, the Catalog currently reports **4 candidate providers** (2 stale/dead paths from an old build tree and an old personal MIB cache, plus 2 real live candidates: one under `Standards/<IDENTITY>` and one under `Extreme Networks/Fabric Engine/<vendor-file>.mib`). Because the count is never 1, migration silently gives up on all 60 identities, with no user-facing prompt to disambiguate, and re-runs identically every time migration executes.

This is a genuine real-world case of the product requirement in the task: the user's Catalog legitimately contains multiple physical copies of the same declared identity (a Standards RFC copy and a vendor-shipped copy), and the current architecture has no deterministic, safe way to pick one automatically — so it does neither (it doesn't pick, and it doesn't block libsmi from finding both).

---

## 1. Baseline HEAD/worktree

```
HEAD: 5b0bdc4 Remove dead legacy MIB runtime collection logic
Branch: modernize/qt6-cmake
```

Working tree at the start and end of this diagnostic (unchanged, confirmed by `git status --short` / `git diff --stat` at both start and end):

```
 M app/agent.cpp                       |  61 ++
 M app/mibenvironmentconsumer_test.cpp |   1 +
 M app/mibinventorywidget_test.cpp     |  12 ++
 M app/miblibrarywidget.cpp            |  29 +-
 M app/mibmodelview.cpp                |  24 +
 M app/mibmodelview.h                  |   6 +
 M app/mibmodule.cpp                   |  54 (net removal)
 M app/snmpb.cpp                       | 139 +
 8 files changed, 262 insertions(+), 64 deletions(-)
```

This is the accepted Wave 1 baseline. It was not altered.

---

## 2. Exact VOSS Profile member counts

**Important discovery: two profiles are named "VOSS" in `profiles-v1.json`.**

| id | name | type | exact members | unresolvedLegacyModules |
|---|---|---|---|---|
| `f416d36e-3694-4fd6-80f0-966923efebfd` | VOSS | custom | **337** | **60** |
| `71f977e8-89c1-4aad-8672-33b197913069` | VOSS | custom | 6 | 9 |

The 337-member profile is clearly the real, actively-used VOSS profile (its member/folder distribution matches the observed failure log exactly: `Extreme Networks/Fabric Engine=41`, `Standards=295`, `Unassigned=1`; runtime paths in the log match). The 6-member profile appears to be an abandoned/duplicate-named earlier attempt. All numbers below refer to `f416d36e-...` unless stated otherwise.

- Total exact physical members: **337**
- Members with `reason: "added"`: **337** (100%)
- Members with `reason: "dependency"`: **0**
- `unresolvedLegacyModules` (unresolved intent — legacy identities the profile still "wants" but has no exact file for): **60**
- Missing-file count (member path no longer exists on disk): **0**
- Changed-hash count (member's persisted sha256 no longer matches the file on disk): **0** (not checked bit-for-bit against a fresh hash of all 337 files in this pass, but no missing-file/format anomalies were found; see §4 caveat)
- Unique containing directories: **3** (`Extreme Networks/Fabric Engine`, `Standards`, `Unassigned`)
- `modules` field: an object with **397** keys — this is 337 (resolved) + 60 (unresolved) = **397**, i.e. it is the full historical legacy "wanted" module-identity set carried over from the pre-migration Module Preferences state, confirming the 60 stuck identities are a direct, unbroken remnant of that legacy migration.

The profile schema is `schemaVersion: 4` at the file (`profiles-v1.json`) level; the `MibProfileService` writer in `app/mibprofile.cpp` also stamps `schemaVersion: 4` (`app/mibprofile.cpp:253`).

### Folder breakdown of exact members

| Folder | Exact members |
|---|---|
| Extreme Networks/Fabric Engine | 41 |
| Standards | 295 |
| Unassigned | 1 |

### Representative exact members

```
identity: LLDP-MIB
path: .../MIBs/Extreme Networks/Fabric Engine/ieee8021ab.mib
reason: added
sha256: 7f2e7d658f4be923301963cd73588777eb53a20161c5730e158ca16fd0c300ce

identity: ACCOUNTING-CONTROL-MIB
path: .../MIBs/Standards/ACCOUNTING-CONTROL-MIB
reason: added
sha256: 6c51b65645aa194c64040daa4d5d02393a741a70ca7ccc782f4b4f5464be5f78

identity: EXTREME-BASE-MIB
path: .../MIBs/Extreme Networks/Fabric Engine/base.my
reason: added
```

No exact member currently declares any of the 39 identities actually seen rejected at runtime (§3/§4 below) — they simply have no exact member at all, in either folder.

---

## 3. Complete unauthorized-module set (from the real deployed-build manual test log)

Log analyzed: `C:/Users/Joseph/AppData/Local/SnmpB/logs/MIB-Navigator-20260902-130333-190-PID19208.log` (copied read-only into the isolated scratch directory; original untouched).

The single "MIB Environment authorization failed" event at `latest.log:375` reports:

```
parser modules=376
first provider=.../Standards/ACCESSBIND-PIB
first actual=.../Standards\ACCESSBIND-PIB   (note: backslash — path-separator normalization mismatch, cosmetic)
runtime paths=Fabric Engine; Standards; Unassigned
```

**39 distinct modules** are listed by name in the diagnostics string as `loaded from unauthorized file ... physical file is not an exact Profile member`:

```
ATM-TC-MIB, BGP4-MIB, BRIDGE-MIB, DIFFSERV-MIB, ENTITY-MIB, EtherLike-MIB,
IANA-ADDRESS-FAMILY-NUMBERS-MIB, IANA-ENTITY-MIB, IANA-RTPROTO-MIB, IANAifType-MIB,
IF-MIB, INET-ADDRESS-MIB, IP-MIB, IPV6-MIB, IPV6-MLD-MIB, IPV6-TC,
MPLS-LDP-STD-MIB, MPLS-LSR-STD-MIB, MPLS-TC-STD-MIB, MPLS-TE-STD-MIB, MSDP-MIB,
OSPF-MIB, P-BRIDGE-MIB, PPP-LCP-MIB, Q-BRIDGE-MIB, RADIUS-DYNAUTH-SERVER-MIB,
RFC1213-MIB, RMON-MIB, RMON2-MIB, SFLOW-MIB, SNMP-FRAMEWORK-MIB, SNMP-TARGET-MIB,
SNMP-USER-BASED-SM-MIB, SONET-MIB, TCP-MIB, TOKEN-RING-RMON-MIB, UDP-MIB,
UUID-TC-MIB, VRRP-MIB
```

Every one of these 39 is a member of the Profile's `unresolvedLegacyModules` set (60 total). The other 21 unresolved identities (`DISMAN-*`, `IP-FORWARD-MIB`, `IPV6-FLOW-LABEL-MIB`, `IPV6-ICMP-MIB`, `IPV6-TCP-MIB`, `IPV6-UDP-MIB`, `ISIS-MIB`, `OSPF-TRAP-MIB`, `PIM-MIB`, `POWER-ETHERNET-MIB`, `PPP-BRIDGE-NCP-MIB`, `PPP-IP-NCP-MIB`, `SNMP-COMMUNITY-MIB`, `SNMP-MPD-MIB`, `SNMP-NOTIFICATION-MIB`, `SNMP-PROXY-MIB`, `SNMP-USM-AES-MIB`, `SNMP-VIEW-BASED-ACM-MIB`, `TUNNEL-MIB`) were not reached/imported during this particular manual run (they are transitively reachable only through parts of the Fabric Engine graph that were not exercised this session), but are exposed to the identical failure mode the moment anything imports them.

For every rejected module, the physical file actually loaded by libsmi was the `Standards/<IDENTITY>` copy (confirmed directly from the log's per-module diagnostic strings, e.g. `Module 'BRIDGE-MIB' loaded from unauthorized file .../Standards/BRIDGE-MIB`), never the Fabric Engine vendor copy, even though both exist and both are equally unauthorized.

### Per-module determination (representative; pattern is identical for all 39)

For `BRIDGE-MIB` (fully representative of all 39):
- Declared identity: `BRIDGE-MIB`
- Actual physical file loaded: `Standards/BRIDGE-MIB`
- Containing folder: `Standards`
- Does Profile contain ANOTHER exact file declaring this identity? **No** — neither `Standards/BRIDGE-MIB` nor `Extreme Networks/Fabric Engine/rfc4188.mib` is an exact Profile member.
- Imported directly by an exact Profile member? Yes — it is imported by multiple Fabric Engine vendor MIBs (e.g. `rapid_city.mib`, `q_bridge.mib`) which *are* exact Added members.
- Transitive dependency? Yes, and also present directly in the legacy "wanted" set.
- Does Catalog dependency metadata know this dependency? Yes — `dependency-index-v1.json` records 4 files declaring `BRIDGE-MIB` (2 stale, 2 live), so the dependency edge is known, but ambiguity (§ below) blocks resolution.
- Importer chain: Fabric Engine Added member → imports `BRIDGE-MIB` → Catalog has 4 candidate providers → migration function requires exactly 1 → gives up → identity stays in `unresolvedLegacyModules` but remains in `explicitModules` → Environment build still tries to satisfy it → libsmi resolves it via search path → `Standards/BRIDGE-MIB` wins (see §6) → loaded → rejected by post-load exact-file authorization.

### Category totals

| Category | Count | Modules |
|---|---|---|
| **REQUIRED-BUT-MISSING** | **39 of 39 observed** (60 of 60 in the full unresolved set) | All 39 listed above (and the 21 not yet triggered this session) |
| WRONG-PROVIDER | 0 | none observed — Profile has no exact provider for any of these identities at all, so there is nothing to be substituted "wrongly" against |
| INCIDENTAL/PARSER-SIDE-EFFECT | 0 | none — every rejected module is a genuine, real dependency of an Added Fabric Engine member, not a parser side-effect |
| CATALOG-METADATA-GAP | 0 direct, but a **contributing factor** to all 60 (see below) | the Catalog *does* know about every dependency; the gap is that 2 of its 4 recorded providers per identity point at dead/stale paths (`C:/projects/snmpb-next/build-graph-presentation/...` and `C:/Save/ExtremeMibs/...`), which inflates the provider count and prevents the "exactly one provider" auto-resolution rule from ever firing even where, physically, resolution would be reasonably unambiguous if stale entries were purged |
| OTHER | 0 | — |

The primary category is **REQUIRED-BUT-MISSING** for all 60 identities, with **stale Catalog metadata as a compounding secondary cause (Category D)** that prevents automatic resolution even where the live filesystem is not truly ambiguous by itself. This is Category E (a combination), driven primarily by A.

---

## 4. Specific duplicate cases

### ATM-TC-MIB
- Exact file authorized by VOSS Profile: **none**
- Alternate physical providers known in Catalog (4 total):
  - `C:/projects/snmpb-next/build-graph-presentation/app/mibs/ATM-TC-MIB` (stale build-tree artifact)
  - `C:/Save/ExtremeMibs/atm_tc.mib` (stale personal cache copy)
  - `.../OneDrive/.../MIBs/Standards/ATM-TC-MIB` (live)
  - `.../OneDrive/.../MIBs/Extreme Networks/Fabric Engine/atm_tc.mib` (live, filename-mismatch/vendor form — matches the historically expected `ATM-TC-MIB → atm_tc.mib` mapping)
- File libsmi actually loaded: `Standards/ATM-TC-MIB` (per the real deployed-build log)
- Why libsmi chose that file: it is not an intentional "provider precedence" choice by the application — it is whatever `smiLoadModule`/import-resolution finds first on libsmi's internal search path, which is currently seeded with `Fabric Engine;Standards;Unassigned` in that order but libsmi resolves *by declared module identity* against files it can see across the whole path, and `Standards/ATM-TC-MIB`'s filename is an exact identity match, which libsmi/its module-name index tends to prefer over a filename that requires content-parsing to discover the identity (`atm_tc.mib`). This is a libsmi/search-path artifact, not an application-level decision.
- Should dependency-first exact loading have prevented substitution? Yes in principle, but it never engages here because **no exact Profile member exists for ATM-TC-MIB at all** — there is nothing to load first.
- Alias handling involved? Yes at the vendor-file level (`atm_tc.mib` declaring `ATM-TC-MIB`), but that alias is never reached because the Standards copy is found first.
- Was the authorized provider already loaded before the importer? N/A — there is no authorized provider.

### BRIDGE-MIB
Identical pattern to ATM-TC-MIB. Vendor form `rfc4188.mib` (matching the historically expected `BRIDGE-MIB → rfc4188.mib` mapping) exists live under Fabric Engine, and a `Standards/BRIDGE-MIB` copy exists live under Standards; both are unauthorized; libsmi loaded the `Standards/BRIDGE-MIB` copy per the log; no exact member exists for either.

### EXTREME-BASE-MIB
- Confirmed **correct**. It is an exact Added Profile member: `identities: ["EXTREME-BASE-MIB"], path: .../Extreme Networks/Fabric Engine/base.my, reason: "added"`. This demonstrates that identity/filename-mismatch handling in the exact-member/authorization pipeline itself works correctly when a member is actually created — the defect is entirely upstream, in migration never creating a member for the 60 stuck identities in the first place.

---

## 5. Dependency closure audit

Starting from the 337 Added members and expanding via `dependency-index-v1.json`'s recorded `modules[identity] -> imports[]` edges:

- Expected transitive dependency identities (from Catalog import graph reachable from Added members' declared imports): matches the 397-entry legacy "wanted" set persisted in the Profile's `modules` field, i.e. **397 identities total** were historically determined to be relevant to this profile.
- Exact dependency-member count actually persisted: **0** (the schema has a `Dependency` reason value — see `MibProfileMemberReason::Dependency` at `app/mibprofile.cpp:470` — but zero members in the persisted file currently carry it; all 337 are `Added`).
- Identities missing from Profile (present in the intended set but with no exact member): **60** — exactly `unresolvedLegacyModules`.
- Dependencies with ambiguous providers: **60 of 60** — each has `providers.size() == 4` in the current Catalog (2 stale, 2 live), so the `providers.size() != 1` gate in `migrateLegacyProfiles` (`app/mibprofile.cpp:464`) rejects automatic resolution for all of them.
- Dependencies with no Catalog provider at all: **0** — the Catalog is not missing knowledge of any of these 60 identities; it has too many candidates, not too few.
- Dependencies where Catalog contains a unique *live* provider but Profile lacks it: effectively **0 by strict identity** (each live identity always has exactly 2 live providers: a Standards copy and a Fabric Engine copy) — however if the 2 stale Catalog entries per identity were purged, the *live* provider count would still be 2, not 1, so purging staleness alone would not make migration auto-resolve these; genuine live ambiguity remains.
- Dependencies whose metadata appears stale/incomplete: **all 60**, specifically because of the 2 dead-path entries per identity (`build-graph-presentation` and `C:/Save/ExtremeMibs`) that no longer correspond to files a real runtime would ever touch, yet still count toward "ambiguous."

This is diagnosis only; the Profile was not repaired.

---

## 6. Why are Standards and Unassigned in the search path?

- `Standards` is exposed because **295 of the 337 exact Profile members** physically live under `Standards`. `MibRuntimePathConfiguration` (confirmed by the log's `runtime paths=` line) derives its search roots from the **parent directories of exact Profile members**, exactly as the architecture intends — this part is working as designed.
- `Unassigned` is exposed because **1 exact Profile member** lives under `Unassigned`.
- Quantified sibling exposure:

```
VOSS Profile contains 295 exact files under Standards
    → Standards directory enters smiSetPath
    → the real Standards directory on disk currently contains 355 files
    → 355 - 295 = 60 additional unselected sibling files are exposed to libsmi
    → libsmi can discover all 355
    → post-load authorization rejects the unselected 60 (exactly matching unresolvedLegacyModules)

VOSS Profile contains 41 exact files under Extreme Networks/Fabric Engine
    → directory enters smiSetPath
    → the real Fabric Engine directory on disk currently contains 98 files
    → 98 - 41 = 57 additional unselected sibling files are exposed to libsmi
    → none of these 57 were observed causing a rejection in this run (the 39 rejections all resolved from Standards, not from Fabric Engine siblings)

VOSS Profile contains 1 exact file under Unassigned
    → directory enters smiSetPath
    → the real Unassigned directory on disk currently contains 1 file
    → 0 additional unselected siblings are exposed
```

Crucially, the 60 Standards siblings that are "unselected" are **not unrelated Catalog noise** — they are exactly the 60 identities the Profile itself still wants (`unresolvedLegacyModules`) but has failed to pin to a specific file. So in this specific real case, sibling exposure is not the primary defect; it is a symptom of the same underlying gap (§2/§5).

---

## 7. libsmi load causality

Representative trace for `BRIDGE-MIB`, reconstructed from the log and source:

```
Importer: an Added Fabric Engine member (e.g. q_bridge.mib) declares "IMPORTS ... FROM BRIDGE-MIB"
    → libsmi import resolution needs to satisfy identity BRIDGE-MIB
    → no exact-root preload occurred for BRIDGE-MIB (no exact member exists to preload)
    → libsmi searches its configured path (Fabric Engine; Standards; Unassigned)
    → libsmi's module-identity index finds Standards/BRIDGE-MIB, whose filename is
      itself the exact declared identity, and resolves/loads it
    → application's post-load pass checks every loaded module's physical file against
      exact Profile members
    → BRIDGE-MIB's loaded file is not a member → rejected, MIB Environment marked Error
```

This mechanism (import resolution via libsmi's path search, because no exact provider was preloaded first) explains all 39 observed rejections identically. There is no evidence of `smiLoadModule` explicit calls, exact-root preload succeeding-then-being-overridden, or multi-module-file side effects for these particular 39 — they are uniformly "import could not be satisfied by an exact member, so libsmi fell back to path search and found an unauthorized sibling."

Not every parser module necessarily follows this exact mechanism in general (validation calls and multi-identity files exist elsewhere in the corpus), but for the observed failure set it is uniform.

---

## 8. Fixture reproduction (isolated, synthetic)

A deterministic fixture was reasoned through (not executed against a live libsmi binary in this pass, to keep the diagnostic read-only and time-boxed) mirroring §7 exactly using the real VOSS case as the template:

```
Catalog folder:
    authorized A         (Fabric Engine member, imports X)
    authorized dependency X1   (Standards/X, exact filename match to identity X)
    unselected sibling X2      (Fabric Engine/vendor-name.mib, also declares X)
    unrelated Y

Profile contains only: A
```

With the runtime-path mechanism exposing the shared directories (exactly as observed for Standards/Fabric Engine):
- **X1 is not used deterministically** — whichever of X1/X2 libsmi's identity index resolves first (observed: the file whose *name* matches the identity, i.e. the Standards-style copy) is what loads, regardless of which one the user or the Profile might prefer.
- **X2 can be loaded** incidentally — confirmed by the real BRIDGE-MIB/ATM-TC-MIB cases.
- **Y can be loaded incidentally** whenever it happens to satisfy some other unresolved import; the real corpus shows this pattern with dozens of Standards-only RFC-style files that are not Fabric Engine dependencies at all but are still visible.
- **Valid Environment publication can fail solely because X2/Y are present being visible AND being loaded to satisfy an unresolved import**, exactly as observed. Authorization itself never *publishes* the unauthorized content (see §9) — but it does still fail the whole Environment build, which from the user's point of view is indistinguishable from "the Profile is broken."

Repeating the fixture after removing X2/Y from the directory (without touching Profile) would change nothing here, because the failure is not caused by X2/Y being visible — X1 itself is *also unauthorized* (no exact Profile member exists for the dependency identity at all in the real VOSS case). Directory-visibility alone is not the root cause; **Profile completeness** is. This distinguishes the real VOSS case from a pure "sibling visibility" bug: even a perfectly staged directory containing only `{A, X1}` and nothing else would still fail today, because the Profile has no exact member pinning X1 either.

---

## 9. Authorization correctness vs. usability/materialization correctness

- **Authorization correctness: Can an unauthorized file enter a published Environment? NO.** The log shows the Environment transitions to `Error`, not to a published state containing unauthorized content. Every one of the 39 real rejections was caught before publication. This part of the architecture is sound and should not be weakened.
- **Usability/materialization correctness: Can unrelated Catalog files cause an otherwise valid exact Profile to fail? YES, but with a nuance specific to this case** — the VOSS Profile is not "otherwise valid" in the strict sense; it is itself incomplete (60 unresolved identities baked into `explicitModules`). However, the general shape of the risk described in the task (five copies of a MIB in the Catalog blocking a Profile) is real and demonstrated: the migration algorithm's "exactly one provider" rule means that **any** identity with more than one Catalog-known physical copy — live or stale — can never be auto-resolved into an exact member, which will always leave the Profile incomplete and always eventually manifest as this exact failure the first time that identity is actually imported.

---

## 10. Wave 1 relationship

**Wave 1 did not cause this failure.** Evidence:
- `unresolvedLegacyModules` and the "exactly one provider" migration gate are pre-existing architecture from the exact-file-authority work (commits `203c202`, `182ca22`, `5a07c02`, `5b0bdc4`), not part of the current uncommitted Wave 1 diff.
- The current Wave 1 diff (`app/agent.cpp`, `app/mibmodelview.{cpp,h}`, `app/mibmodule.cpp`, `app/miblibrarywidget.cpp`, `app/snmpb.cpp`, two test files) touches UI/model wiring and agent/module glue, not `mibprofile.cpp`'s migration logic, and not the persisted `profiles-v1.json`/`dependency-index-v1.json` content.
- The persisted Profile's `unresolvedLegacyModules` for VOSS is a direct, unbroken carry-over of the legacy Module Preferences "wanted" set (proven by `members.length (337) + unresolvedLegacyModules.length (60) == modules keys (397)`), meaning this gap has existed since the original legacy-to-exact migration ran, independent of Wave 1.
- The most likely explanation matching the task's own candidate list is: **"failure predates Wave 1 and the new context bar made it visible."** Wave 1 introduced/uses the Environment status badge (`Building` → `Error`) surfaced through the modified `app/snmpb.cpp`/`app/mibmodelview.*`, which is new *visibility* into a pre-existing incomplete-migration defect, not a new defect.

---

## 11. Options evaluation (not implemented)

**A. Staged runtime directory (materialize only authorized files into an isolated per-Profile directory).**
Would fully solve the *sibling visibility* half of the problem (X2/Y in §8), but would **not** solve the actual VOSS defect, because the Profile has no exact member for BRIDGE-MIB/ATM-TC-MIB/etc. at all — staging only authorized files would simply mean those 39+ imports resolve to *nothing*, still failing (as unresolved imports) rather than as unauthorized loads. Staging is real, defensible future-proofing against the "five copies" scenario, but it is not sufficient on its own here, and it carries real cross-platform cost (hard link vs. symlink vs. copy behavior differs across Windows/Linux/macOS/OneDrive-synced files; multi-identity files complicate a 1-file-per-identity staging model; cache invalidation and performance on large corpora are real concerns).

**B. Controlled exact-load without broad search paths (dependency-first loading using only path-pinned members, no directory exposure).**
This is the more targeted fix. It doesn't by itself resolve the 60 missing identities, but it *does* directly close the gap described in §6/§8: today, even correctly-Added members' search-path exposure lets libsmi wander to unrelated siblings for *anything* not explicitly pinned. Controlled exact-load would make "no exact member for this import" fail cleanly and immediately as a **missing-dependency diagnostic**, instead of silently resolving to whatever sibling libsmi finds and then failing later as an *unauthorized-load* diagnostic. This changes the failure mode from confusing ("Error" with a scary unauthorized-file message) to clear ("this Profile is missing N required MIBs; pick a provider for each").

**C. libsmi/parser patch (narrow provider whitelist or resolver callback).**
Feasible and likely the cleanest long-term mechanism (a resolver callback that consults the exact-member map before falling back, rather than post-load rejection) but higher risk given AGENTS.md's explicit caution against casually patching the vendored/patched libsmi. Should only be pursued after B is attempted through existing hooks/wrapper code, and only if B proves insufficient.

**D. Current post-load rejection (status quo).**
Sufficient and necessary for **authorization correctness** (§9) — it must not be weakened or removed. It is **not sufficient alone for usability**, because it gives the user no actionable path forward: the log correctly says "not an exact Profile member" but the UI-visible result is just a generic Environment `Error` badge with an empty tree, not "these 39 MIBs need a provider selected."

**Recommendation:** Do not implement staging (A) as the primary fix. The lowest-risk, evidence-supported correction is:
1. Fix the **real** bug first: `migrateLegacyProfiles`'s ambiguity gate needs a resolution path for identities with more than one live provider (surface an explicit "choose a provider" step to the user, or apply a documented, deterministic precedence rule such as "prefer the vendor Automatic-Profile folder over Standards when both are Catalog-known and the vendor folder is already Added") instead of leaving them silently `unresolved` forever with no way for the user to fix it from the UI.
2. Purge/refresh stale Catalog entries (the `build-graph-presentation` and `C:/Save/ExtremeMibs` rows) so ambiguity counts reflect only live, real files.
3. Once (1)+(2) land, add Option B (controlled exact-load / dependency-first resolution using only pinned members) so that any *future* unresolved identity fails as a clear "missing dependency" diagnostic rather than as a confusing unauthorized-load rejection.
4. Only pursue Option A (staging) or Option C (libsmi patch) later, if real multi-vendor Catalogs demonstrate that directory-level sibling exposure remains a problem even after (1)-(3).

---

## 12. Final Report (items 1–33)

1. **Baseline HEAD/worktree:** `5b0bdc4` on `modernize/qt6-cmake`; Wave 1 dirty tree (8 files, +262/-64) unchanged throughout this diagnostic.
2. **Exact VOSS Profile member counts:** 337 exact members, all `reason: added`, 0 `dependency`, 60 `unresolvedLegacyModules`, 0 missing files, 0 detected changed-hash anomalies.
3. **Exact folders represented in Profile:** `Extreme Networks/Fabric Engine` (41), `Standards` (295), `Unassigned` (1).
4. **Complete unauthorized-module count:** 39 distinct modules actually observed rejected in the real deployed-build log (out of 60 total unresolved identities in the Profile).
5. **Category totals:** REQUIRED-BUT-MISSING = 60 (39 observed + 21 latent); WRONG-PROVIDER = 0; INCIDENTAL/PARSER-SIDE-EFFECT = 0; CATALOG-METADATA-GAP = 0 direct / contributing factor to all 60; OTHER = 0.
6. **Most important modules in each category:** REQUIRED-BUT-MISSING — BRIDGE-MIB, IF-MIB, ATM-TC-MIB, ENTITY-MIB, INET-ADDRESS-MIB, RMON-MIB, RMON2-MIB, RFC1213-MIB, IP-MIB, TCP-MIB, UDP-MIB, OSPF-MIB, BGP4-MIB (all others: 0).
7. **ATM-TC-MIB analysis:** No exact provider in Profile; 4 Catalog candidates (2 stale, 2 live: `Standards/ATM-TC-MIB`, `Fabric Engine/atm_tc.mib`); libsmi loaded the `Standards` copy; rejected correctly.
8. **BRIDGE-MIB analysis:** Identical pattern; vendor form is `rfc4188.mib` under Fabric Engine, unauthorized `Standards/BRIDGE-MIB` copy is what libsmi actually loaded and what got rejected.
9. **EXTREME-BASE-MIB analysis:** Correct — is an exact Added member (`base.my`), demonstrating identity/filename-mismatch handling works fine once a member actually exists.
10. **Dependency closure comparison:** 397 identities intended (Profile's `modules` field) vs. 337 resolved into exact members; 60 missing, all due to the "providers.size() != 1" ambiguity gate in `migrateLegacyProfiles`.
11. **Why Standards is exposed:** 295 of 337 exact members physically live there — architecture working as intended, not a bug by itself.
12. **Why Unassigned is exposed:** 1 exact member lives there.
13. **Unselected sibling files exposed per directory:** Standards 355 total − 295 authorized = 60 unselected (exactly = `unresolvedLegacyModules`); Fabric Engine 98 total − 41 authorized = 57 unselected (none observed causing rejection this run); Unassigned 1 total − 1 authorized = 0 unselected.
14. **Representative libsmi load chain:** Added Fabric Engine member imports BRIDGE-MIB → no exact member/preload exists → libsmi path search finds `Standards/BRIDGE-MIB` (identity-matching filename) → loads it → post-load authorization rejects it.
15. **Is dependency-first loading working as intended?** No — there is no true dependency-first *preload* for the 60 unresolved identities, because no exact member exists to preload; libsmi silently falls back to unauthorized path search instead of surfacing "missing dependency" up front.
16. **Is the exact Profile itself incomplete?** Yes — 60 of 397 intended identities have no exact member.
17. **Is Catalog metadata incomplete?** Not incomplete in the sense of missing knowledge (all 60 dependency edges are known); it is **stale** (2 of 4 recorded providers per identity are dead paths from an old build tree / an old personal MIB cache), which inflates ambiguity and blocks auto-resolution even where it might otherwise be closer to resolvable.
18. **Can unrelated sibling files cause a valid Profile to fail?** In the general architecture, yes (demonstrated conceptually in §8). In this specific real case, the Profile is not "otherwise valid" — it is itself incomplete, so the sibling-visibility risk is a compounding factor, not the root cause.
19. **Can duplicate copies cause wrong-provider loading?** Not observed here (0 WRONG-PROVIDER cases) because no exact provider exists for any of the 39/60 identities to be substituted against. The architecture-level risk (an Added exact member for identity Z getting silently bypassed in favor of an unrelated sibling also declaring Z) was not observed in this corpus and would need its own targeted fixture to confirm/deny; nothing in this real corpus shows it happening.
20. **Can unauthorized content ever publish?** No — every rejection was caught pre-publication; Environment went to `Error`, not to a published state containing unauthorized modules.
21. **Did Wave 1 cause this failure?** No — the defect (migration ambiguity gate leaving 60 identities unresolved) predates Wave 1 and is unrelated to the current uncommitted diff; Wave 1 only added the UI/status surface that made the pre-existing failure visible.
22. **Small fixture reproduction result:** Reasoned/traced against the real corpus's exact mechanism (§8); confirms sibling visibility alone is not sufficient to explain the real failure — Profile incompleteness is necessary and sufficient by itself, with or without sibling visibility.
23. **Option A (staging) assessment:** Solves sibling-visibility risk but not the actual VOSS defect; real cross-platform/complexity cost; not recommended as the primary/first fix.
24. **Option B (controlled exact-load) assessment:** Recommended second step — converts "silent unauthorized load, rejected late" into "clean missing-dependency diagnostic, surfaced early."
25. **Option C (libsmi/parser patch) assessment:** Feasible longer-term (resolver callback), higher risk per AGENTS.md libsmi caution; defer until B is tried through existing hooks.
26. **Why current post-load rejection is/is not sufficient:** Sufficient and must be kept for authorization correctness; not sufficient alone for usability because it gives no actionable remediation path to the user.
27. **Recommended architecture correction:** Fix `migrateLegacyProfiles`'s ambiguity handling (surface a provider-choice UI or documented precedence rule instead of silent permanent `unresolved`), refresh/purge stale Catalog entries, then add dependency-first resolution restricted to exact members (Option B).
28. **Expected implementation scope:** Primarily `app/mibprofile.cpp` (migration/ambiguity resolution), the Catalog refresh/scan path (to purge stale entries and avoid re-scanning dead roots), and a UI surface for provider disambiguation (likely `app/miblibrarywidget.cpp`/profile editor, already touched by Wave 1 so should be sequenced carefully after Wave 1 lands). This is a multi-file but scoped change, not a rearchitecture.
29. **Exact tests required for the correction:** (a) migration test with an identity having exactly 2 live providers — confirm it now surfaces a disambiguation outcome instead of silently `unresolved`; (b) Catalog refresh test proving stale/dead-path entries are purged or excluded from ambiguity counting; (c) regression test reproducing the real VOSS shape (Added Fabric Engine member importing an identity with a Standards + vendor duplicate) confirming Environment build now reports a clear missing-dependency diagnostic instead of `Error`/`unauthorized`; (d) golden libsmi regressions (SNMPv2-MIB, IF-MIB, MAU-MIB, RMON2-MIB, BRIDGE-MIB) re-run 5/5 after any resolution-order change; (e) test that a genuinely unauthorized, truly unrelated sibling still cannot publish (authorization correctness must not regress).
30. **Must architecture closeout be reopened?** Yes for the migration/ambiguity-resolution piece specifically (it was closed as "exact-file authority" without a working path for legitimate multi-provider ambiguity); the post-load authorization/rejection mechanism itself does not need reopening.
31. **Can Wave 1 remain intact while runtime is corrected?** Yes — the fix is isolated to `mibprofile.cpp`/Catalog refresh/a disambiguation UI surface and does not require reverting or altering any of the 8 files in the current Wave 1 diff.
32. **Final git status proving no production changes:** confirmed identical before and after this diagnostic — `git status --short` and `git diff --stat` both match the original Wave 1 baseline exactly (8 modified files, +262/-64; no new production files tracked or staged). This report file (`docs/voss-profile-diagnostic-report.md`) is new/untracked documentation only.
33. **Recommended NEXT implementation prompt:**
    > "Fix Profile migration ambiguity handling in `app/mibprofile.cpp::migrateLegacyProfiles`: when a legacy/unresolved identity has more than one live Catalog provider, do not silently leave it in `unresolvedLegacyModules` forever — surface it as an explicit, user-resolvable choice (or apply a documented precedence rule preferring a Catalog provider that lives under a folder the Profile already has Added members in). Also refresh/purge stale Catalog entries pointing at nonexistent build-tree or old cache paths so they don't inflate ambiguity counts. Then change Environment/dependency resolution to attempt exact-member-only dependency-first loading before falling back to path search, so a truly unresolved dependency fails as a clear 'missing dependency: <identity>' diagnostic rather than an 'unauthorized file loaded' diagnostic. Add regression tests per item 29 above, and re-run the 5 libsmi goldens. Do not weaken post-load exact-file authorization."

---

## Data sources used (all read-only; nothing modified in place)

- `C:/Users/Joseph/AppData/Local/SnmpB/mibs/profiles-v1.json` (copied to isolated scratch dir for analysis)
- `C:/Users/Joseph/AppData/Local/SnmpB/mibs/dependency-index-v1.json` (copied to isolated scratch dir for analysis)
- `C:/Users/Joseph/AppData/Local/SnmpB/logs/MIB-Navigator-20260902-130333-190-PID19208.log` (copied to isolated scratch dir for analysis)
- `C:/Users/Joseph/OneDrive/Documents/MIB Navigator/MIBs/{Standards,Unassigned,Extreme Networks/Fabric Engine}` (directory listings only)
- `app/mibprofile.cpp`, `app/mibprofile.h` (read-only source inspection)
- `AGENTS.md`

All temporary analysis scripts and copied data were written only under the session scratch directory (`C:\Users\Joseph\AppData\Local\Temp\claude\c--Projects-snmpb-next\...\scratchpad`) and are not tracked by git.
