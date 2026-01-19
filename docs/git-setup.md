ULTRA-MINIMAL PROCEDURE — LOCAL & PERSONAL GIT SETUP
===================================================

SCOPE
-----
This document defines the mandatory setup and operating rules for:
- local development environments
- personal GitHub forks
- safe contribution to the upstream repository

It applies to all contributors and all workflows.


REPOSITORY ROLES
----------------
UPSTREAM  : authoritative repository (read-only by git)
PERSONAL  : contributor fork (integration / CI)
LOCAL     : working copy (only place where work happens)


FLOW (ASCII)
------------
            +----------------------------------+
            |  UPSTREAM (authority)           |
            |  xymon-monitoring/xymon         |
            |  Pull Request + merge (UI)      |
            +----------------------------------+
                         ↑
                         |
            +----------------------------------+
            |  PERSONAL FORK (origin)         |
            |  bonomani/xymon                 |
            |  CI / staging                   |
            +----------------------------------+
                         ↑
                         |
            +----------------------------------+
            |  LOCAL (working copy)           |
            |  developer machine              |
            +----------------------------------+


HARD INVARIANT
--------------
Outside CI branches, the following MUST always be true:

bonomani/xymon:main        == xymon-monitoring/xymon:main
bonomani/xymon:4.x-master  == xymon-monitoring/xymon:4.x-master


HARD RULES
----------
- No git command ever targets upstream
- All upstream interaction is done via GitHub UI only
- All git commands operate LOCAL ⇄ PERSONAL only
- main and 4.x-master on the fork are mirrors only
- Any divergence on mirror branches is temporary and disposable


PREREQUISITES — INSTALLATION
----------------------------

Recommended environment:
- Linux or WSL (preferred, matches CI)

Linux / WSL:
sudo apt update
sudo apt install -y git gh
# (use your distribution package manager if different)

macOS:
brew install git gh

Windows (native, fallback):
winget install Git.Git GitHub.cli
# or
choco install git gh


STEP 0 — AUTHENTICATE gh (ONCE, BROWSER-BASED)
---------------------------------------------
Run:
gh auth login

Choose:
- GitHub.com
- HTTPS
- Authenticate Git with GitHub credentials: Yes
- Login method: Browser

Behavior:
- gh opens a browser automatically
- If not possible, gh prints a URL

If a URL is shown:
- Open it manually in your browser
- Follow the on-screen instructions

Verify:
gh auth status


STEP 1 — CREATE PERSONAL FORK
-----------------------------
Using GitHub UI:
- Fork xymon-monitoring/xymon
- Result: bonomani/xymon


STEP 2 — CLONE PERSONAL FORK (LOCAL)
------------------------------------
Using gh (recommended):
gh repo clone bonomani/xymon
cd xymon

OR using git:
git clone https://github.com/bonomani/xymon.git
cd xymon


STEP 3 — DECLARE UPSTREAM (READ-ONLY)
-------------------------------------
git remote add upstream https://github.com/xymon-monitoring/xymon.git
git remote set-url --push upstream DISABLED


STEP 4 — VERIFY REMOTES
-----------------------
git remote -v

Expected:
origin    https://github.com/bonomani/xymon.git (fetch)
origin    https://github.com/bonomani/xymon.git (push)
upstream  https://github.com/xymon-monitoring/xymon.git (fetch)
upstream  DISABLED (push)


INVARIANT VERIFICATION (LOCAL ⇄ PERSONAL)
-----------------------------------------
Verify strict equality using commit SHAs.

# main
git rev-parse main
git rev-parse origin/main

# 4.x-master
git rev-parse 4.x-master
git rev-parse origin/4.x-master

If SHAs are equal → OK  
If SHAs differ → reset required


INVARIANT RESET (REPAIR)
------------------------
Restore local mirror branches to the fork state.

git fetch origin
git reset --hard origin/main
git reset --hard origin/4.x-master


UPSTREAM MERGE (PERSONAL → UPSTREAM)
-----------------------------------
Purpose: apply validated changes to the authoritative repository.

Procedure (GitHub UI only):
- Open a Pull Request:
  bonomani/xymon:<branch> → xymon-monitoring/xymon:<branch>
- Review and merge are performed by upstream maintainers

Notes:
- Contributors do not merge upstream PRs themselves
- No git command ever targets upstream


POST-MERGE SYNC (MANDATORY)
--------------------------
After an upstream merge, restore fork mirrors.

On GitHub UI (bonomani/xymon):
- Sync fork on branch main
- Sync fork on branch 4.x-master

Then locally:
git fetch origin
git reset --hard origin/main
git reset --hard origin/4.x-master


WHAT THIS GUARANTEES
-------------------
- Upstream cannot be modified accidentally
- Personal fork is the only writable remote
- Local state is always verifiable and repairable
- Mirror branches remain mechanically enforceable
- CI and governance rules rely on a stable base


KEY RULE (ONE LINE)
------------------
If a git command could modify upstream, it must not exist.

