GIT GUIDELINES - BEST PRACTICES
==============================

PURPOSE
-------
This document gives simple, practical advice
for working with Git in this project.

Official Git rules and repository governance are defined in:
- [git-rules.md](git-rules.md)

This document does not define rules.


SCOPE
-----
These guidelines apply to everyday development work.


WORKING GUIDELINES
------------------
- Keep commits focused so the project history stays easy to read
- Use your personal fork to test and integrate changes


BRANCH PRACTICES
----------------
- Use a separate branch for any change that is more than a small fix
- Keep branches short-lived and remove them after merge


COMMIT MESSAGES
---------------
- Write clear commit messages
- Avoid unnecessary or purely automatic commits
- A squashed commit should still clearly describe the change


PULL REQUEST PRACTICES
----------------------
- Split large changes into smaller PRs when possible
- If a change must be large, explain why in the PR description


REVIEW PRACTICES
----------------
- Explain what your PR does and why
- Avoid mixing refactoring and behavior changes if possible


THINGS TO AVOID
---------------
- Very large or unclear commits
- Long-lived branches without a clear purpose
- Risky changes on main branches without review
- Changing shared history without coordination


IF SOMETHING GOES WRONG
-----------------------
- Find the last working state
- Fix or restore carefully


KEY IDEA
--------
Prefer clear, careful work over rigid process.
