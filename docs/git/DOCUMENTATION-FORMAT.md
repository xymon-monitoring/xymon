DOCUMENTATION FORMAT
====================

SCOPE
-----
This document defines formatting rules for documentation files
in this repository.


MARKDOWN FORMAT RULES
---------------------
Use these rules as written when editing documentation.
- Use setext-style headings (`Title` + `====` / `----`)
- Use fenced code blocks for diagrams and commands
- Use bullet or numbered lists for enumerations
- Use relative Markdown links for repo files (outside code blocks)


NOTES
-----
Audience: contributors editing Markdown docs and maintainers managing documentation output.

- `docs/manpages/` contains generated HTML; do not edit these files by hand
- `build/makehtml.sh` regenerates manpage HTML from `*.1`, `*.5`, `*.7`, `*.8` sources
- Markdown and HTML handling:
  - Today, HTML is generated from manpage sources only
  - Integrating Markdown into the manpage pipeline adds tooling and complexity
  - Short term: link to Markdown from HTML indexes
  - Markdown is justified because it is easy to edit, renders on GitHub, and is tool-friendly
  - Long term: add a separate MD->HTML build step if needed
- `build/makehtml.sh` does not touch `docs/git/` (safe location for Markdown)
