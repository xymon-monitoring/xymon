#!/usr/bin/env python3
"""Post-process mandoc(1) HTML so it matches the pages Xymon has always shipped.

mandoc converts the manual pages more faithfully than man2html - it keeps the
definition-list structure exactly, and gives every tag a named anchor - but it
does not produce the navigation man2html generates, and it lays the page out
differently. This script closes both gaps, so that switching converters changes
what the pages are made of without changing what they look like.

Reads one page on standard input, writes it to standard output.

  1. cross-page links   "xymongen(1)" becomes a link to ../man1/xymongen.1.html,
                        for the pages this tree actually ships. References to
                        pages we do not ship are left as text rather than
                        pointed at a 404.
  2. per-page index     the "Index" block man2html appends, built from mandoc's
                        section anchors.
  3. banner and footer  the shape man2html gives them, minus its wall-clock
                        stamp, which is not reproducible.
  4. section spacing    man2html precedes each heading with a non-breaking
                        space, which is a real line box; reproduce it so the
                        vertical rhythm is unchanged.
"""
import re
import sys

# "name(N)", with or without the font markup a man(7) page may wrap it in.
XREF = re.compile(r'<([ib])>([A-Za-z0-9_.+-]+)\((\d)\)</\1>')
BARE = re.compile(r'\b([A-Za-z0-9_.+-]+)\((\d)\)')
TAG = re.compile(r'(<[^>]*>)')
HEAD = re.compile(r'<h1 class="Sh" id="([^"]+)"><a[^>]*>(.*?)</a></h1>', re.S)
HEADTBL = re.compile(
    r'<table class="head">.*?<td class="head-ltitle">([^<]*)</td>\s*'
    r'<td class="head-vol">([^<]*)</td>.*?</table>', re.S)
FOOTTBL = re.compile(r'<table class="foot">.*?</table>', re.S)
FOOTDATE = re.compile(r'<td class="foot-date">([^<]*)</td>')
SHOPEN = re.compile(r'(<h1 class="Sh")')

# mandoc and man2html name the volumes differently. Keep man2html's, so the
# banner text does not change under the switch.
VOLUME = {
    "1": "User Commands",
    "5": "File Formats",
    "7": "Environments, Tables, and Troff Macros",
    "8": "Maintenance Commands",
}


def linkify(html, known):
    def link(name, sect):
        target = "%s.%s.html" % (name, sect)
        if target not in known:
            return None
        return '<a href="../man%s/%s">%s</a>(%s)' % (sect, target, name, sect)

    def font_repl(m):
        font, name, sect = m.groups()
        a = link(name, sect)
        return m.group(0) if a is None else "<%s>%s</%s>" % (font, a, font)

    html = XREF.sub(font_repl, html)

    # Second pass over text nodes only: SEE ALSO sections write their
    # references as plain text, and man(7) has no macro that marks them.
    out, in_anchor = [], False
    for part in TAG.split(html):
        if part.startswith("<"):
            low = part.lower()
            if low.startswith("<a "):
                in_anchor = True
            elif low.startswith("</a"):
                in_anchor = False
            out.append(part)
        elif in_anchor or not part.strip():
            out.append(part)
        else:
            out.append(BARE.sub(
                lambda m: link(m.group(1), m.group(2)) or m.group(0), part))
    return "".join(out)


def add_index(html):
    heads = HEAD.findall(html)
    if not heads:
        return html
    items = "\n".join(
        '<dt><a href="#%s">%s</a></dt>' % (i, re.sub(r"\s+", " ", t).strip())
        for i, t in heads)
    block = ('<h1 class="Sh" id="INDEX"><a class="permalink" href="#INDEX">'
             'Index</a></h1>\n<dl class="Bl-tag">\n' + items + '\n</dl>\n')
    # The insertion point is the footer table, which rewrite_footer() replaces
    # later in main(). Run these two in the other order and str.replace finds
    # nothing, drops the index from every page and says so to no one - so say
    # it here instead of returning the document unchanged.
    anchor = '</div>\n<table class="foot"'
    if anchor not in html:
        sys.exit("manpage-html: no footer table to insert the index before - "
                 "has rewrite_footer() already run?")
    return html.replace(anchor, block + anchor, 1)


def rewrite_banner(html):
    m = HEADTBL.search(html)
    if not m:
        return html
    title, vol = m.group(1).strip(), m.group(2).strip()
    name, _, sect = title.partition("(")
    sect = sect.rstrip(")")
    vol = VOLUME.get(sect, re.sub(r"\s*Manual$", "", vol))
    d = FOOTDATE.search(html)
    updated = d.group(1).strip() if d else ""
    banner = ('<h1 class="head-name">%s</h1>\n'
              'Section: %s (%s)<br/>Updated: %s<br/>'
              '<a href="#INDEX">Index</a>\n'
              '<a href="../index.html">Return to Main Contents</a><hr/>\n'
              % (name, vol, sect, updated))
    return html[:m.start()] + banner + html[m.end():]


def rewrite_footer(html):
    """Close the page as man2html does, without its wall-clock stamp.

    man2html writes "Time: 23:08:11 GMT, September 04, 2019" from the clock at
    generation time; the committed pages carry two different values one second
    apart, so re-generating them diffs every page even when nothing changed.

    No date replaces it. The obvious candidate is the .TH date, but it is
    already in the banner as "Updated: Version 4.3.31: 7 Aug 2026", and putting
    it here as well reads as "Time: Version 4.3.31: 7 Aug 2026" - a label that
    announces a time followed by something that is not one.
    """
    foot = ('<hr/>\n'
            'This document was created by mandoc, using the manual pages.\n')
    return FOOTTBL.sub(foot, html, count=1)


def match_spacing(html):
    """man2html precedes every heading with <A NAME=...>&nbsp;</A>, which is a
    real line box: it blocks margin collapsing and adds a line of height above
    each section. Reproducing the markup keeps the spacing right in any
    browser, which guessing at a CSS margin would not."""
    return SHOPEN.sub(r'<p>&#160;</p>\n\1', html)



DTID = re.compile(r'<dt id="([^"]*)"><a class="permalink" href="#\1">(.*?)</a></dt>', re.S)


def rename_anchors(html):
    """Name each definition anchor after its label, not after mandoc's slug.

    mandoc truncates at the first hyphen and keeps the argument placeholder, so
    "--sender=STRING" becomes #sender=STRING and, worse, "--no-pin" and
    "--no-cookies" both become #no, distinguished only by a ~2 suffix that
    depends on document order - inserting an option above silently repoints
    every link below it.

    The label's first token, cut at "=", is stable and readable: #sender,
    #no-pin, #LOAD. Collisions are numbered the way mandoc numbers its own.
    """
    seen = {}
    mapping = {}

    def slug(label):
        t = re.sub(r"<[^>]*>", "", label).strip()
        t = t.split()[0] if t.split() else ""
        t = t.split("=")[0].strip("-[](){}<>\"',.:;")
        return t

    def collect(m):
        old, label = m.group(1), m.group(2)
        new = slug(label)
        if not new:
            return m.group(0)
        seen[new] = seen.get(new, 0) + 1
        if seen[new] > 1:
            new = "%s~%d" % (new, seen[new])
        mapping[old] = new
        return '<dt id="%s"><a class="permalink" href="#%s">%s</a></dt>' % (new, new, label)

    html = DTID.sub(collect, html)
    # Any in-page link to a renamed anchor has to follow.
    for old, new in mapping.items():
        if old != new:
            html = html.replace('href="#%s"' % old, 'href="#%s"' % new)
    return html


def main():
    # makehtml.sh exports LC_ALL=C, and under a C locale Python decodes stdin
    # as ASCII up to 3.6; 3.7 promotes it to UTF-8 (PEP 538/540). The pages are
    # ASCII today, so nothing breaks either way - but the first accented
    # character in a manual page would fail with a traceback on the older
    # interpreter. Say what the encoding is instead of inheriting it.
    # reconfigure() itself only exists from 3.7 on, which is why it runs
    # before the selftest exit: an interpreter too old to have it must be
    # refused by the selftest, not fail halfway through the pipeline.
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")

    # Called by makehtml.sh before the pipeline starts, to check that this
    # script runs at all - see the comment there.
    if "--selftest" in sys.argv[1:]:
        return

    known = set(sys.argv[1:])
    html = sys.stdin.read()
    html = linkify(html, known)
    html = rename_anchors(html)
    html = add_index(html)
    html = match_spacing(html)
    html = rewrite_banner(html)
    html = rewrite_footer(html)
    sys.stdout.write(html)


if __name__ == "__main__":
    main()
