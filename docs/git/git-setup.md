GIT SETUP - PROCEDURES
=====================

PURPOSE
-------
This document defines the canonical setup and maintenance procedure
for contributors using a personal fork.

Authoritative governance rules live in:
- [git-rules.md](git-rules.md)

This document is procedural and scoped to the contributor workflow.


AUTHORITATIVE FLOW (PHASED)
==========================

```text
PHASE 1
  ┌───────────────────────────────┐
  │ GitHub UI                     │
  │ Create personal fork          │
  │ <user>/xymon                  │
  └───────────────┬───────────────┘
                  │
PHASE 2           │ git clone
  ┌───────────────▼───────────────┐
  │ LOCAL WORKING COPY             │
  │ developer machine              │
  └───────────────┬───────────────┘
                  │
PHASE 3           │ git remote add upstream
                  │ upstream = fetch-only
                  │
PHASE 4           │ verify remotes
                  │
PHASE 5           │ baseline verification
                  │ compare against upstream
                  │
PHASE 6           │ controlled restore (if required)
                  │
PHASE 7           │ local development
                  │ git commit / git push
                  │
                  ▼
  ┌───────────────────────────────┐
  │ PERSONAL FORK                 │
  │ <user>/xymon                  │
  │ (push allowed)                │
  └───────────────┬───────────────┘
                  │
PHASE 8           │ Pull Request (GitHub UI)
                  │
                  ▼
  ┌───────────────────────────────┐
  │ UPSTREAM (READ-ONLY)          │
  │ xymon-monitoring/xymon        │
  │ authoritative source          │
  └───────────────┬───────────────┘
                  │
PHASE 9           │ Sync fork (GitHub UI)
                  │
                  ▼
  LOCAL + FORK realigned
```


RULES
-----
- Upstream is the single authoritative truth source.
- All baseline verification is performed against upstream.
- Origin (personal fork) is a writable mirror used only as a sync and PR source.
- Upstream is fetch-only and modified ONLY via Pull Requests.
- Push is allowed ONLY to the personal fork.
- main  = stable / release
- devel = active development baseline


PHASE 0 - PREREQUISITES
----------------------
- A GitHub account
- Git installed locally
- A personal fork is allowed; direct upstream pushes are not

Reference:
- [git-installation.md](git-installation.md)


PHASE 1 - CREATE PERSONAL FORK (GITHUB UI)
-----------------------------------------
On GitHub:
- Fork xymon-monitoring/xymon
- Result:
  <your-github-username>/xymon


PHASE 2 - CLONE PERSONAL FORK (LOCAL)
------------------------------------
Using gh (recommended):
```
gh repo clone <your-github-username>/xymon
cd xymon
```

Or using git:
```
git clone https://github.com/<your-github-username>/xymon.git
cd xymon
```


PHASE 3 - DECLARE UPSTREAM (FETCH-ONLY)
--------------------------------------
The upstream remote represents the authoritative repository and is
intentionally configured as fetch-only.
```
git remote add upstream https://github.com/xymon-monitoring/xymon.git
git remote set-url --push upstream DISABLED
```


PHASE 4 - VERIFY REMOTES
-----------------------
```
git remote -v
```


PHASE 5 - BASELINE VERIFICATION (AGAINST UPSTREAM)
-------------------------------------------------
Baseline verification is always performed against upstream.
```
git fetch upstream
git diff main upstream/main
git diff devel upstream/devel
```


PHASE 6 - CONTROLLED RESTORE (OPTIONAL)
--------------------------------------
Non-destructive attempt:
```
git fetch upstream
git rebase upstream/main
```

Destructive restore (tracked files only):
```
git stash
git reset --hard upstream/main
git stash pop
```

Warnings:
- Tracked changes are discarded.
- Untracked files are preserved.
- git clean is intentionally NOT used.
- Back up anything important before running a destructive reset.

Backup options (recommended):
```
git stash -u
```
```
cp -a . ../xymon-backup
```


PHASE 7 - DEVELOPMENT (LOCAL)
-----------------------------
- Branch from main  for fixes.
- Branch from devel for development.
- Commit locally.
- Push to origin only.


PHASE 8 - UPSTREAM INTEGRATION (PERSONAL -> UPSTREAM)
-----------------------------------------------------
GitHub UI:
- Open Pull Request:
  <user>/xymon:<branch> -> xymon-monitoring/xymon:<target-branch>


PHASE 9 - POST-MERGE SYNC
------------------------
GitHub UI:
- Sync fork on main.
- Sync fork on devel.

Optional local verification:
```
git fetch upstream
git diff main upstream/main
git diff devel upstream/devel
```


END OF PROCEDURE
----------------
This procedure is complete for the defined scope and relies on
GitHub UI actions and referenced governance documents.
