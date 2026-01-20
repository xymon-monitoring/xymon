GIT CONTRIBUTION FLOW - LOCAL -> PERSONAL -> UPSTREAM
====================================================

WELCOME
-------
This guide shows a simple and safe way to contribute.


AUTHORITATIVE FLOW (STEP-BASED)
===============================

```text
STEP 1
  ┌───────────────────────────────┐
  │ UPSTREAM (READ-ONLY)          │
  │ xymon-monitoring/xymon        │
  │ authoritative source          │
  └───────────────┬───────────────┘
                  │
                  │ git fetch / git diff
                  │
STEP 2            │
  ┌───────────────▼───────────────┐
  │ PERSONAL FORK                 │
  │ <user>/xymon                  │
  │ writable mirror               │
  └───────────────┬───────────────┘
                  │
                  │ branch creation only
                  │
STEP 3            │
  ┌───────────────▼───────────────┐
  │ LOCAL WORKING COPY            │
  │ developer machine             │
  │ commit changes                │
  └───────────────┬───────────────┘
                  │
                  │ git push
                  │
                  ▼
  ┌───────────────────────────────┐
  │ PERSONAL FORK                 │
  │ feature branch                │
  └───────────────┬───────────────┘
                  │
STEP 5 / 6        │ Pull Request
                  │
                  ▼
  ┌───────────────────────────────┐
  │ UPSTREAM (READ-ONLY)          │
  │ review and merge              │
  └───────────────┬───────────────┘
                  │
STEP 7            │ cleanup
                  │
                  ▼
  LOCAL + FORK cleaned
```


RULES
-----
- Upstream is the authoritative truth source.
- All verification is done against upstream.
- All writes go to the personal fork.
- Direct development on `main` and `devel` is allowed but discouraged.
- `main` and `devel` are preferred as branching bases and merge targets.
- All upstream changes happen via Pull Requests only.


STEP 1 - CHECK UPSTREAM STATUS
------------------------------
Verify the latest upstream state before creating any work branch.

This step validates your local view against the authoritative source
(upstream), not against your fork.

Commands:
```
git fetch upstream
git diff main upstream/main
git diff devel upstream/devel
```

Alternatively, use "Sync fork" in the GitHub UI.


STEP 2 - CREATE A WORK BRANCH
-----------------------------
Avoid committing directly on `main` or `devel`; branch from them instead.

From `main`:
```
git checkout main
git pull origin main
git switch -c <branch>
git push -u origin <branch>
```

From `devel`:
```
git checkout devel
git pull origin devel
git switch -c <branch>
git push -u origin <branch>
```

IMPORTANT:
Direct commits on `main` or `devel` are discouraged.
Work branches are preferred for non-trivial changes.


STEP 3 - MAKE YOUR CHANGE
-------------------------
```
git add <files>
git commit -m "<message>"
git push
```


STEP 4 - SYNC YOUR FORK WITH UPSTREAM (RECOMMENDED)
---------------------------------------------------
Before opening any PR, make sure your fork is not behind upstream.

If it is, sync it using the GitHub UI:
- Click "Sync fork"
- Choose "Update branch"

IMPORTANT:
`main` and `devel` are moving targets: they advance as upstream evolves.

Do NOT sync if you have commits on `main` or `devel`.
GitHub will warn that syncing will overwrite those changes.

Move any work in progress to a dedicated branch first.
This is why committing directly on `main` or `devel` is forbidden.


STEP 5 - OPTIONAL (RECOMMENDED): OPEN A FORK PR
----------------------------------------------
It is recommended to open a Pull Request in your fork first,
in order to run CI and validate changes before opening
the upstream PR.


STEP 6 - OPEN THE UPSTREAM PR
-----------------------------
Open the upstream PR:
```
<your-github-username>/xymon:<branch>
-> xymon-monitoring/xymon:main (or devel)
```


STEP 7 - CLEAN UP
-----------------
After merge:
```
git branch -d <branch>
git push origin --delete <branch>
```
