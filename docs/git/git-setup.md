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
PHASE 7           │ contribution workflow
                  │ see git-contribution-flow.md
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

PHASE 2A - VERIFY / CONFIGURE GIT IDENTITY
-----------------------------------------

Verify:
```
git config --show-origin --get user.name
git config --show-origin --get user.email
```

Configure if needed:
```
git config --global user.name "Username"
git config --global user.email "your@email"
```
GitHub-provided no-reply addresses are valid:
<id+username@users.noreply.github.com>


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

This step checks your local branches directly against upstream,
so you see the authoritative state instead of your fork.
```
git fetch upstream
git diff main upstream/main
git diff devel upstream/devel
```

Expected result:
- No output means your branch matches upstream.
- Differences mean your branch is ahead or behind and should be aligned before you branch.


PHASE 6 - CONTROLLED RESTORE (OPTIONAL)
--------------------------------------
Non-destructive attempt (from the current local branch):
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


PHASE 7 - CONTRIBUTION WORKFLOW
-------------------------------
Day-to-day development, PR flow, and cleanup are defined in:
- [git-contribution-flow.md](git-contribution-flow.md)


END OF PROCEDURE
----------------
This procedure is complete for the defined scope and relies on
GitHub UI actions and referenced governance documents.
