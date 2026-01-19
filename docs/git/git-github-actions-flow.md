GIT GITHUB ACTIONS FLOW - CI VIA PERSONAL FORK
==============================================

PURPOSE
-------
This document defines the CI-specific contribution workflow
used when modifying or validating GitHub Actions.

Canonical rules live in:
- [git-rules.md](git-rules.md)
The general contribution flow is defined in:
- [git-contribution-flow.md](git-contribution-flow.md)


SCOPE
-----
This workflow applies to:
- GitHub Actions changes,
- CI-related configuration under `.github/`.

It covers only:
- Git operations,
- GitHub UI interactions.

It does NOT cover:
- repository setup,
- branch governance,
- Git rules,
- GitHub Actions YAML or job design.


WORKFLOW STEPS
--------------

STEP 1 - ENSURE BASELINES ARE SYNCED (UI)
-----------------------------------------
Ensure your personal fork is aligned with upstream before starting CI work.

On GitHub (personal fork UI):
- Sync branch `main`
- Sync branch `4.x-master`


STEP 2 - CREATE A CI WORK BRANCH
--------------------------------
Create a dedicated branch for CI changes.

From `main`:
```
git checkout main
git switch -c ci-<topic>
git push -u origin ci-<topic>
```


STEP 3 - APPLY CI CHANGES
-------------------------
Modify CI-related files (typically under `.github/`).

```
git add .github/
git commit -m "CI: update GitHub Actions"
git push
```


STEP 4 - OPTIONAL: FORK-SIDE CI VALIDATION
------------------------------------------
Run GitHub Actions before upstream submission.

On GitHub (personal fork UI):
- Open a Pull Request: ci-<topic> -> main
- Let GitHub Actions run

Fork-side validation is:
- limited to the personal fork,
- temporary,
- non-authoritative,
- used for CI verification only.


STEP 5 - PREPARE CI FOR 4.x-master (IF REQUIRED)
------------------------------------------------
If the same CI changes must apply to `4.x-master`:

```
git checkout 4.x-master
git switch -c ci-<topic>-4x
git cherry-pick <commit-sha>
git push -u origin ci-<topic>-4x
```


STEP 6 - SUBMIT UPSTREAM PULL REQUESTS
--------------------------------------
Submit CI changes to upstream via GitHub UI.

- <your-fork>/xymon:ci-<topic>
  -> xymon-monitoring/xymon:main
- <your-fork>/xymon:ci-<topic>-4x
  -> xymon-monitoring/xymon:4.x-master


STEP 7 - CLEANUP
----------------
Remove temporary CI branches after completion.

Locally:
```
git branch -d ci-<topic> ci-<topic>-4x
```

On the personal fork:
```
git push origin --delete ci-<topic>
git push origin --delete ci-<topic>-4x
```


KEY PRINCIPLE
-------------
This document defines only a CI-specific workflow.
