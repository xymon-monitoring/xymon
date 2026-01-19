GIT GOVERNANCE - CANONICAL RULES
================================

DOCUMENT AUTHORITY
------------------
This document is the single authoritative source for Git governance
in this project.

All rules defined here are normative.
All other Git-related documents (processes, workflows, CI flows)
must defer to this document and must not redefine these rules.

Any deviation from this document is considered a governance defect.


SCOPE
-----
This document defines the mandatory setup and operating rules for:
- local development environments
- personal GitHub forks
- safe contribution to the upstream repository

It applies to all contributors and all workflows.


REPOSITORY ROLES
----------------
UPSTREAM  : authoritative repository (read-only via git)
PERSONAL  : contributor fork (integration, testing, CI)
LOCAL     : developer working copy


ROLE FLOW (ASCII)
-----------------
            +---------------------------------+
            |  UPSTREAM (authority)           |
            |  xymon-monitoring/xymon         |
            |  Pull Request + merge (UI)      |
            +---------------------------------+
                         ^
                         |
            +---------------------------------+
            |  PERSONAL FORK (origin)         |
            |  <your-github-username>/xymon   |
            |  development / CI               |
            +---------------------------------+
                         ^
                         |
            +---------------------------------+
            |  LOCAL (working copy)           |
            |  developer machine              |
            +---------------------------------+


REFERENCE BASELINES
-------------------
At synchronization points, the following invariants MUST hold:

- <your-github-username>/xymon:main == xymon-monitoring/xymon:main
- <your-github-username>/xymon:4.x-master == xymon-monitoring/xymon:4.x-master

These branches are baseline branches. They represent the upstream state.

They MAY diverge during active development. Any divergence MUST be intentional, visible, and temporary.


HARD RULES
----------
- No git command ever targets upstream
- All git commands operate LOCAL <-> PERSONAL only
- main and 4.x-master are baseline branches
- Direct development on baseline branches is allowed but discouraged
- Work branches are preferred for non-trivial changes
- Any divergence from baselines must be intentional
