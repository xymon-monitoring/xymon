GIT ACTIONS PERSONAL WORKFLOW
============================

PURPOSE
-------
This document defines the **personal (non-authoritative) workflow**
for adapting, running, and validating GitHub Actions in a personal fork
or downstream repository.

It is intended for contributors and downstream users who want to:
- reuse upstream GitHub Actions,
- adapt them to local repository constraints,
- run Actions on demand or on arbitrary branches.

Canonical governance and upstream lifecycle rules live in:
- [git-actions-upstream-workflow.md](git-actions-upstream-workflow.md)
- [git-rules.md](git-rules.md)

This document does NOT define upstream behavior.
It describes **local usage and customization only**.


SCOPE
-----
This workflow applies to:
- personal forks,
- downstream repositories,
- local experimentation and validation of GitHub Actions.

It covers:
- syncing upstream Action branches,
- adapting workflows for local use,
- enabling manual and branch-agnostic execution.

It does NOT cover:
- upstream promotion rules,
- branch governance,
- authoritative CI or release processes,
- Action YAML best practices beyond what is required for execution.


RELATION TO UPSTREAM ACTIONS
----------------------------
Upstream GitHub Actions may be maintained in dedicated branches
(e.g. `action/build`, `action/test`, `action/release`).

In a personal context:
- upstream Action branches are **sources**, not authorities,
- local modifications are expected and allowed,
- divergence is acceptable and intentional.

Upstream remains the reference for future updates.


PERSONAL ACTION FLOW (ASCII)
============================

This diagram shows how a user consumes and adapts upstream Actions
in a personal or downstream repository.

────────────────────────────────────────────────────────────

STEP 1 (IF USED)
  ┌───────────────────────────────┐
  │ UPSTREAM ACTION BRANCH        │
  │ action/<name> (if used)       │
  └───────────────┬───────────────┘
                  │ fetch / sync
                  ▼
  ┌───────────────────────────────┐
  │ PERSONAL FORK / REPO          │
  │ action/<name>                 │
  └───────────────┬───────────────┘
                  │
STEP 2            │ local adaptation
                  │
                  ▼
  ┌───────────────────────────────┐
  │ PERSONAL ACTION VARIANT       │
  │ runs on demand / any branch   │
  └───────────────┬───────────────┘
                  │
STEP 3            │ execution
                  ▼
  LOCAL / PERSONAL VALIDATION

────────────────────────────────────────────────────────────


WORKFLOW STEPS
--------------

STEP 1 - SYNC UPSTREAM ACTIONS (IF USED)
----------------------------------------
If you use upstream Action branches, sync the desired Action branch into
your personal fork or downstream repository.

This step assumes you have an `upstream` remote configured.

Example:
```
git fetch upstream
git checkout -b action/<name> upstream/action/<name>
git push -u origin action/<name>
```

This creates a local copy that can be modified freely.


STEP 2 - ADAPT ACTION FOR PERSONAL USE
--------------------------------------
Modify the workflow under `.github/` to fit your repository.

Typical adaptations include:
- adjusting paths or build commands,
- changing matrix values,
- disabling unused jobs,
- adapting permissions.

These changes are **local-only** and must not be pushed upstream.


STEP 3 - ENABLE MANUAL EXECUTION
--------------------------------
To allow on-demand execution, ensure the workflow includes
a `workflow_dispatch` trigger.

Example:
```yaml
on:
  workflow_dispatch:
```

This allows manual runs from the GitHub UI.
Upstream repositories should keep `workflow_dispatch` restricted
(see [git-actions-upstream-workflow.md](git-actions-upstream-workflow.md)).

STEP 4 - ALLOW EXECUTION ON ANY BRANCH
-------------------------------------
Upstream workflows often restrict execution to specific branches.

For personal use, remove or relax branch filters such as:

```yaml
branches: [ main, devel ]
```

Options:
- remove branch filters entirely, or
- use a wildcard:

```yaml
branches:
  - "**"
```

This enables execution on feature branches and test branches.

STEP 5 - REMOVE UPSTREAM-ONLY CONSTRAINTS
-----------------------------------------
Upstream workflows may include restrictions that do not apply
in a personal context.

Review and adjust:
- repository name checks:
  `if: github.repository == 'xymon-monitoring/xymon'`
- environment protections,
- organization-scoped permissions.

Such guards should remain upstream but may be removed locally.

STEP 6 - SECRETS AND PERMISSIONS
--------------------------------
Personal forks do not inherit upstream secrets.

If required:
- define your own secrets in the fork,
- downgrade permissions where possible,
- avoid assuming access to signing or release credentials.

Actions requiring protected secrets may not be fully testable
in a personal context.

STEP 7 - EXECUTE AND ITERATE
----------------------------
After adaptation, Actions can be run:
- manually via GitHub UI,
- on any branch push,
- as part of local validation workflows.

Iteration is expected and unrestricted.

LIMITATIONS
-----------
Personal Action workflows:
- are non-authoritative,
- may diverge from upstream,
- may not fully reflect upstream execution context.

They must never be assumed to represent upstream behavior.

KEY PRINCIPLES
--------------
- Upstream defines **what is canonical**.
- Personal workflows define **what is usable locally**.
- Divergence in personal forks is expected and acceptable.
- No personal Action changes flow upstream automatically.
- Promotion upstream follows [git-actions-upstream-workflow.md](git-actions-upstream-workflow.md).
