GIT GUIDELINES - BEST PRACTICES
===============================

PURPOSE
-------
This document provides non-normative guidance and best practices
for working with Git in this project.

Git governance, repository authority, and baseline rules are defined
in `[git-rules.md](git-rules.md)`.

This document does not define rules.


SCOPE
-----
These practices apply to day-to-day development activities
and complement the canonical setup and workflows.


WORKING GUIDELINES
------------------
- Prefer work branches for non-trivial changes
- Keep commits focused, readable, and intentional
- Maintain a clear and understandable history
- Use personal forks as integration and testing spaces
- Clean up temporary branches once work is complete


THINGS TO AVOID
---------------
- Large, unfocused commits
- Long-lived temporary branches
- Unreviewed risky changes on baseline branches
- Rewriting shared history without coordination


FAILURE HANDLING
----------------
If something goes wrong:
- Pause and assess the situation
- Identify the last known good state
- Restore deliberately
- Continue with intent


KEY PRINCIPLE
-------------
Prefer clarity, responsibility, and intent over rigid process.
