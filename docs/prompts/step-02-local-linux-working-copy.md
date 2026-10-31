# Task: Redesign Plan step 2: local Linux working copy

Read `AGENTS.md` and `docs/redesign-plan.md` first. They govern this task. In
this task, read them from the old working tree, because the local clone does
not exist yet. This task is **step 2 only**. Do not start any other build-order
step.

## Background

All development now happens on this Ubuntu Linux VM. Until now, the working
tree was used from Windows and is stored on a Synology NAS:

    OLD = /run/user/1001/gvfs/smb-share:server=mrincodex.local,share=ai-projects/Personal/Software/snmpb-next

It is reached through a GNOME gvfs SMB mount. That is too slow for development
(one `git status` takes minutes), and some git commands fail there, for
example `git show :path` returns "Invalid argument".

New working copy:

    NEW = ~/src/snmpb-next   (on the VM's local disk; does not exist yet)

## Goal

1. `NEW` is a git clone on local disk at the same commit and branch as `OLD`,
   with the same remotes.
2. All of the user's uncommitted work in `OLD` is carried into `NEW` as
   uncommitted changes, with line-ending-only differences removed and real
   content byte-identical apart from line endings.
3. `NEW` has a `.gitattributes` line-ending policy.
4. A backup bare repository exists on the NAS as remote `nas`, holding the
   committed history.
5. A first Linux build and test baseline exists, with every failure
   classified.
6. `OLD` is left untouched.

## Known facts (re-verify; stop and report if materially different)

- `OLD` branch: `modernize/qt6-cmake` at `4913624`, the same as
  `origin/modernize/qt6-cmake`. Local `main` also exists.
- Remotes: `origin` = `https://github.com/transientuser/snmpb-next.git`,
  `upstream` = `https://github.com/gmfcd128/snmpb.git`.
- No submodules, no LFS-tracked files, no stashes, and a single worktree.
- No `.gitattributes`. `OLD` was checked out on Windows with CRLF conversion.
  From Linux, `git ls-files --eol` shows:
  - 2565 `i/lf w/lf`
  - **1860 `i/lf w/crlf`** (line-ending-only noise)
  - **36 `i/lf w/mixed`** (mostly files with real uncommitted edits)
  - **11 `i/crlf w/crlf`**, committed with CRLF: `app/snmpb.xml`,
    `installer/win32/snmpb.ini`, `installer/win32/snmpb.nsi`, three
    `libsmi/mibs/iana/*` MIBs, `libsmi/win/config.nmake`,
    `libsmi/win/makefile`, `libsmi/win/smi.def`,
    `libtomcrypt/libtomcrypt.dsp`, `snmp++/snmp++-3.pc.in`
  - 95 `-text` (binary)
- Known untracked files include `app/startupcoordinator.{cpp,h}`,
  `app/workbenchshell.{cpp,h}`, `app/workbenchshell_test.cpp`,
  `snmpb-next.code-workspace`, `.claude/settings.local.json`,
  `docs/redesign-plan.md`, and this prompt file. `AGENTS.md` has real
  uncommitted edits.
- The VM's global git identity is set (`transientuser`). Do not change it.
- Toolchain on the VM: Qt 6.11 system packages, GCC 15, CMake 4.3, Ninja.
  The project requires Qt 6.10 or newer.

## Critical constraint: MIB file bytes

MIB provider identity is SHA-256 of raw file bytes (`app/mibcollection.cpp`,
`app/mibdependencyindex.cpp`). In `NEW`, every MIB, PIB, and libsmi
golden/fixture file must be byte-identical to its committed (index) version.
Never carry over a Windows CRLF working copy of such a file. A MIB file with
real uncommitted content changes is an exception to report, not to convert
silently. Do not change the hashing code.

## Steps

### 1. Preconditions

- If `NEW` already exists, stop and report.
- In `OLD`, record: `git rev-parse HEAD`, branch, `git branch -vv`,
  `git remote -v`, `git stash list`, and `git worktree list`.
- For every local branch in `OLD`, check whether it has commits not on
  `origin`. If any exist, they must reach `NEW` (fetch from `OLD` as a
  temporary remote, then remove that remote). Report them.
- Save all inventory output to a scratch directory outside both trees, for
  example `~/snmpb-migration/`. Do not write into `OLD`.

### 2. Inventory `OLD` once

Run the slow commands once and save their output:

- `git status --porcelain=v1 -uall`
- `git ls-files --eol`
- `git status --porcelain=v1 --ignored` (report top-level ignored paths
  only; do not copy ignored content such as `build-*` or `manual-validation/`)

Running `git status` in `OLD` may refresh `OLD`'s index stat cache. That is
acceptable. Make no other writes to `OLD`.

### 3. Clone

Clone from `origin` into `NEW` (faster and more reliable than cloning over
gvfs), check out `modernize/qt6-cmake`, create local `main` tracking
`origin/main`, and add the `upstream` remote. Verify that `HEAD` equals `OLD`'s
`HEAD` and that branch tips match.

### 4. Carry over uncommitted work

For each path in the `OLD` status inventory:

- **Modified tracked text file whose index version is LF:** copy from `OLD`,
  removing a trailing CR from each line (`sed 's/\r$//'` or equivalent). Do not
  remove CR characters anywhere else.
- **Tracked file committed with CRLF, or binary (`-text`):** copy bytes
  exactly.
- **Deleted tracked file:** delete it in `NEW`.
- **Untracked file:** copy it. If it is text with CRLF line endings, convert to
  LF. Binary files are copied exactly.
- **MIB/PIB/golden/fixture files:** see the critical constraint.

Then verify:

- `git status` in `NEW` lists only files with real content changes, plus
  untracked files. Files that differed from the index only in line endings must
  not appear.
- For every carried text file, `NEW` content equals the `OLD` content with
  line-ending CRs removed, byte for byte.
- No carried file has mixed line endings (`git ls-files --eol`).
- Report three lists: real changes carried, line-ending-only files dropped as
  noise, and anything you could not classify.

`snmpb-next.code-workspace` and `.claude/settings.local.json` may contain
Windows or NAS paths. Copy them unchanged and report any paths the user should
update.

### 5. `.gitattributes`

Create `.gitattributes` in `NEW` with:

- `* text=auto` as the default (LF in the repository).
- `eol=crlf` for Windows-only build and installer files that need CRLF, such
  as `*.nsi`, the `libsmi/win/` nmake files, `*.def`, `*.dsp`, `*.bat`, and
  `*.cmd`. Check that each file really needs CRLF.
- `-text` (no conversion) for MIB, PIB, and libsmi golden/fixture
  directories, so Windows CI checkouts get the exact committed bytes. Find all
  such locations; MIB files often have no extension.
- Explicit handling for each of the 11 CRLF-committed files, so none appears
  modified and none changes committed bytes.
- Known binary types stay binary.

Verify: after adding the file, `git status` shows no new modified files apart
from `.gitattributes` itself. Do not run `git add --renormalize` and do not
stage anything. If you want to check how a fresh clone behaves, do it in a
throwaway clone under the scratch directory.

### 6. NAS backup repository

*Superseded (2026-10-30): the share is now mounted at `/mnt/nas`, and the bare
repository is `/mnt/nas/Personal/Software/git/snmpb-next.git`. The gvfs path
below no longer applies.*

Create a bare repository at
`/run/user/1001/gvfs/smb-share:server=mrincodex.local,share=ai-projects/Personal/Software/git/snmpb-next.git`,
add it to `NEW` as remote `nas`, and push all local branches and tags. This
pushes committed history only, and the user authorized it as part of this step.
Never push to `origin` or `upstream`.

If creating or pushing to a bare repository over gvfs fails, do not try
workarounds that touch other NAS locations. Stop this sub-step, report the
error, and suggest alternatives (for example SSH to the Synology, or an NFS or
kernel CIFS mount). The rest of the task does not depend on it.

### 7. Linux build and test baseline

In `NEW`:

- Configure and build Debug with Ninja in `build-linux-debug/`. Record all
  warnings separately from errors.
- Run the complete permitted deterministic test suite (`ctest`) and the five
  libsmi golden tests (SNMPv2-MIB, IF-MIB, MAU-MIB, RMON2-MIB, BRIDGE-MIB;
  expected 5/5). Find how the goldens are run.
- Build Release in `build-linux-release/`, install it with `cmake --install`
  to `build-linux-release/install/`, and check the executable with `ldd`. Do
  not launch the GUI.

Classify every failure: genuine product defect, test defect, timing,
environment, or Linux portability (for example tests using `C:/...` paths or
Windows path assumptions).

You may fix Linux build or test failures only if all of these hold:

- the fix is a portability fix with no behavior change on Windows
- it does not touch libsmi parser or scanner sources and regenerates nothing
- it does not change golden expectations
- the total number of files you change for fixes is 10 or fewer

Otherwise, stop fixing and report the failures with your classification.

### 8. Record status

In `NEW`'s `docs/redesign-plan.md`, mark step 2 done, or partially done with a
one-line reason. Change nothing else in that file.

## Do not

- Write to `OLD` (beyond the index refresh noted above), or delete anything
  there.
- Commit, stage, stash, reset, revert, or clean in either tree.
- Push to `origin` or `upstream`.
- Set global git configuration.
- Change file content other than line endings, except permitted portability
  fixes in step 7 and the status line in step 8.
- Change MIB file bytes or MIB hashing code.
- Launch the GUI or contact any network device.
- Start step 3 or any later step.

## Scope guardrail

The one-time line-ending exception in `AGENTS.md` covers dropping
line-ending-only noise. Real changes carried from `OLD` are the user's existing
work, not new scope. New changes you author are limited to `.gitattributes`,
the plan status line, and at most 10 portability-fix files. If you would exceed
that, stop and report.

## Report

Keep it short:

1. `NEW` path, branch, `HEAD`, remotes, and any unpushed commits found.
2. Carried work: real-change files (count and list), noise files dropped
   (count), untracked files copied, deletions applied, anything unclassified.
3. `.gitattributes` content, with a one-line reason for each non-default rule.
4. Verification results (`git status` comparison, byte comparison, MIB hash
   check, `--eol` summary, `git diff --check`).
5. NAS backup: success, or the error and the suggested alternative.
6. Build: warnings summary, Debug result, Release and install result, `ldd`
   result, and exact executable path.
7. Tests: full suite result, golden result, each failure with its
   classification, and any portability fixes made (files and reason).
8. Paths in copied workspace/settings files that need updating.
9. What the user should do next: open `NEW` in VS Code,
   review the carried changes, then commit in this order: (a) `.gitattributes`
   alone, (b) the redesign docs (`AGENTS.md`, `docs/redesign-plan.md`,
   `docs/prompts/`), (c) the in-progress feature work once decided (step 3).
