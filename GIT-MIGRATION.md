GIT-MIGRATION.md
================

Purpose
-------

This repository contains a faithful import of the historical Xymon
SourceForge SVN repository into Git.

The goal of this import is to provide a modern, shared Git base so that
the community can review the full history, consolidate existing patches,
and discuss future maintenance and governance.

This document describes what was done, and just as importantly, what was
*not* done.


What this repository is
-----------------------

- A full import of the SourceForge SVN history of Xymon
- All revisions, branches, and tags preserved
- No functional changes introduced during the migration
- No attempt to clean, squash, or reinterpret history

The original project files (including existing README files) are kept
unchanged and reflect the historical state of the project.


What this repository is NOT
---------------------------

- Not a new upstream release
- Not a cleaned or refactored history
- Not a decision on future branch policy
- Not a replacement for community discussion or consensus

This is strictly an archival and technical migration step.


Migration method
----------------

The migration was performed using standard, open-source tools:

- SourceForge SVN access via `svn://svn.code.sf.net/p/xymon/code`
- Full repository dump using `svnrdump`
- Conversion to Git using `reposurgeon`

The process was designed to maximize fidelity to the original SVN
repository and minimize interpretation or manual intervention.


Scripts
-------

The scripts used for the migration are included in this repository under
the `scripts/` directory.

They are provided so that anyone can:

- Review the exact steps taken
- Reproduce the migration independently
- Verify that no history was altered


Current state
-------------

- Multiple historical branches and tags exist, corresponding to SVN
  branches and release points
- Branch naming reflects the imported history and has not yet been
  rationalized
- No branch has been officially designated as the production or upstream
  default by the community

Any future cleanup, branch consolidation, or governance decisions should
be discussed and agreed upon publicly.


Next steps (discussion)
-----------------------

Possible next steps, subject to community agreement, may include:

- Verifying the correctness of the import
- Agreeing on a minimal maintainer group
- Defining a clear production branch (e.g. 4.3)
- Consolidating downstream patches
- Establishing a contribution and release workflow

These steps are intentionally *not* performed yet.


Feedback
--------

Feedback from users, maintainers, and operators is explicitly requested.

Please focus on:
- Technical correctness of the import
- Missing or incorrect history
- Verification of branches and tags
- Suggestions for next steps

Thank you for taking the time to review this migration.

