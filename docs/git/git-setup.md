GIT SETUP - PROCEDURES
======================

PURPOSE
-------
This document covers setup and maintenance procedures.
Canonical rules live in `[git-rules.md](git-rules.md)`.


PREREQUISITES
-------------
See `[git-installation.md](git-installation.md)`.


STEP 1 - CREATE PERSONAL FORK
-----------------------------
Using GitHub UI:
- Fork xymon-monitoring/xymon
- Result: <your-github-username>/xymon


STEP 2 - CLONE PERSONAL FORK (LOCAL)
------------------------------------
Using gh (recommended):
```
gh repo clone <your-github-username>/xymon
cd xymon
```

OR using git:
```
git clone https://github.com/<your-github-username>/xymon.git
cd xymon
```


STEP 3 - DECLARE UPSTREAM (READ-ONLY)
-------------------------------------
```
git remote add upstream https://github.com/xymon-monitoring/xymon.git
git remote set-url --push upstream DISABLED
```


STEP 4 - VERIFY REMOTES
-----------------------
```
git remote -v
```

Expected:
- origin    https://github.com/<your-github-username>/xymon.git (fetch)
- origin    https://github.com/<your-github-username>/xymon.git (push)
- upstream  https://github.com/xymon-monitoring/xymon.git (fetch)
- upstream  DISABLED (push)


BASELINE VERIFICATION (LOCAL <-> PERSONAL)
------------------------------------------
Inspect divergence between local branches and the fork.

```
git fetch origin
git diff main origin/main
git diff 4.x-master origin/4.x-master
```

No output means the local branch matches the fork.
Differences indicate intentional or accidental divergence.


BASELINE RESTORE (OPTIONAL)
---------------------------
If you decide to realign with the fork:
```
git reset --hard origin/main
git reset --hard origin/4.x-master
```

Use with care: this overwrites local changes.


UPSTREAM INTEGRATION (PERSONAL -> UPSTREAM)
-------------------------------------------
Purpose: apply validated changes to the authoritative repository.

Procedure (GitHub UI only):
- Open a Pull Request:
  <your-github-username>/xymon:<branch> -> xymon-monitoring/xymon:<branch>
- Review and merge are performed by upstream maintainers


POST-MERGE SYNC (RECOMMENDED)
-----------------------------
After an upstream merge, contributors SHOULD realign their fork.

On GitHub UI:
- Sync fork on branch main
- Sync fork on branch 4.x-master

Then locally, if desired:
```
git fetch origin
git diff main origin/main
git diff 4.x-master origin/4.x-master
```
