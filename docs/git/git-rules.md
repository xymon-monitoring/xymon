GIT GOVERNANCE - CANONICAL RULES
================================

GOVERNANCE INTENT
-----------------
These rules protect baseline branches (`main`, `devel`)
and the long-term health of the project.

They do not restrict who may contribute
or how contributors work in their own forks.

All contributions are welcome via Pull Requests.

SCOPE
-----
- local development environments
- personal GitHub forks
- safe contribution to the upstream repository


REPOSITORY ROLES
----------------
UPSTREAM  : authoritative repository (read-only via git)
PERSONAL  : contributor fork (integration, testing, CI)
LOCAL     : developer working copy


REFERENCE BASELINES
-------------------
When you sync, make sure `main` and `devel` match upstream.

BASELINE PROTECTION
-------------------
`main` and `devel` are long-term reference branches.

Any change reaching a baseline branch is considered high-impact
and therefore requires explicit human validation.

- `main` represents a production-quality state
- `devel` is the active development baseline


ACTION BRANCHES
---------------
When used, `action/*` branches are the canonical location
for GitHub Actions changes:

- `action/*` branches should contain only `.github/` content
- Work branches may be created from `action/*`
- Direct commits are discouraged to keep changes reviewable
- Promotion to `main` or `devel` happens via Pull Request


REVIEW AND MERGE POLICY
-----------------------
Integration into `main` or `devel` requires review by two people:
- one author
- one independent reviewer

If an exception is needed, it must be clearly explained
in the Pull Request.

Allowed merge modes include:
- squash
- merge commit
- rebase

HARD RULES
----------
- Upstream is fetch-only; make all changes in PERSONAL or LOCAL
- Treat `main` and `devel` the same way: branch from them and avoid direct commits
- Use work branches for non-trivial changes
- Any divergence from baselines must be intentional


DOCUMENT AUTHORITY
------------------
This document is the authoritative reference for Git governance
in this project.

All other Git-related documents (processes, workflows, CI flows)
must align with these rules and must not redefine them.

If something conflicts with this document, this document takes precedence.
