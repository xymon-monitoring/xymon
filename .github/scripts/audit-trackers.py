#!/usr/bin/env python3
"""Audit the Xymon upstreaming trackers (#29 Terabithia patches, #106 devel commits)
against the Rule block both issues carry.

    ./audit-trackers.py [--repo DIR] [--tera DIR] [--offline A.md B.md]

Exit 0 when every invariant holds, 1 otherwise.  Checks are grouped as:
  structural  - one line in isolation
  relational  - claims one line makes about another   <- where the real defects live
  measured    - the tracker against the code (needs --repo, and --tera for #29)

Filter notes, learned the hard way; changing them causes false positives:
  * pointer lines (PTR) are prose, not items
  * a hash on a line is only a TWIN CLAIM with identity wording, and never when the
    surrounding text negates it ("too low to call a twin", "excludes", "ambiguous")
  * a group ends at the first blank line after its members, not at the next heading
  * "twin tracked on #106" is a cross-reference, not a deferral of the verdict
"""
import argparse, json, re, subprocess, sys, os
from collections import defaultdict

VERDICT = re.compile(r'\*\*(drop\b[^*]*|take as is|take, not as written[^*]*|undecided|delegated → [^*]*)\*\*')
PTR = ('- `86` `102`', '- `6` `19` —', '- `123` — **owned', '- `145` — **owned')
CLAIM = [r'=\s*(?:the\s+)?(?:devel\s+)?`%s`', r'`%s`[^.]{0,40}\btraced 20', r'on `devel` as `%s`',
         r'contained in devel `%s`', r'slice of (?:devel )?`%s`']
NEG = ('too low to call', 'ambiguous', 'not traced', 'no devel twin', 'excludes', 'not measurable')
NOISE = {'Changes', 'debian/changelog', 'configure', 'configure.server', 'configure.client', 'tests/testsuite'}
fails = []
def bad(kind, msg): fails.append((kind, msg))

def items(text):
    out, head = [], ''
    for i, l in enumerate(text.split('\n'), 1):
        if l.startswith('**') and '—' in l: head = l
        if not l.startswith('- ') or any(l.startswith(p) for p in PTR): continue
        m = VERDICT.search(l)
        if not m: continue
        out.append(dict(i=i, l=l, v=m.group(1), head=head,
                        box='x' if l.startswith('- [x]') else ' ' if l.startswith('- [ ]') else '-',
                        icon='🟢' if '🟢' in l else '🟡' if '🟡' in l else '',
                        prs=[int(x) for x in re.findall(r'#(\d+)', l)]))
    return out

def tbt_id(l):
    m = re.match(r'^- (?:\[[ x]\] )?[🟢🟡]? ?`(\d+)[` ]', l)
    return m.group(1) if m else None

def claims(l):
    """devel hashes this #29 line asserts as its counterpart"""
    out = set()
    for h in set(re.findall(r'`([0-9a-f]{7,10})`', l)):
        at = l.find('`%s`' % h)
        if any(x in l[max(0, at-70):at+70] for x in NEG): continue
        if any(re.search(p % h, l) for p in CLAIM): out.add(h)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo'); ap.add_argument('--tera'); ap.add_argument('--offline', nargs=2)
    ap.add_argument('--slug', default='xymon-monitoring/xymon')
    a = ap.parse_args()
    if a.offline:
        A, B = (open(f, encoding='utf-8').read() for f in a.offline)
    else:
        get = lambda n: subprocess.run(['gh', 'issue', 'view', str(n), '--repo', a.slug,
                                        '--json', 'body', '--jq', '.body'],
                                       capture_output=True, text=True, check=True).stdout
        A, B = get(29), get(106)
    I29, I106 = items(A), items(B)
    print("#29: %d items   #106: %d items" % (len(I29), len(I106)))

    K = "**#29's Audit checklist**"
    if A[A.index('### Rule'):A.index(K)] != B[B.index('### Rule'):B.index(K)]:
        bad('structural', 'the Rule block differs between the two issues')

    # ---- structural -------------------------------------------------------
    for src, II in (('29', I29), ('106', I106)):
        for it in II:
            L, v, box, icon = it['l'], it['v'], it['box'], it['icon']
            if len(VERDICT.findall(L)) != 1 and '**split:**' not in L:
                bad('structural', '#%s L%d: %d verdicts' % (src, it['i'], len(VERDICT.findall(L))))
            if box != '-' and v.startswith('drop'):
                bad('structural', '#%s L%d: drop carries a checkbox' % (src, it['i']))
            if box == ' ' and icon == '🟢':
                bad('structural', '#%s L%d: [ ] with 🟢 - green says nothing is owed' % (src, it['i']))
            if box == ' ' and re.search(r'carried by \*\*(PR )?#\d+', L):
                bad('structural', '#%s L%d: prose names a carrier beside [ ]' % (src, it['i']))
            if v.startswith('delegated') and (box != '-' or icon) and '**split:**' not in L:
                bad('structural', '#%s L%d: delegated line carries a box or icon' % (src, it['i']))
            if v.startswith('delegated') and src == '29':
                bad('structural', '#29 L%d: delegation is one-way (#106 -> #29)' % it['i'])

    # ---- relational -------------------------------------------------------
    h2tbt, tbt = defaultdict(set), {}
    for it in I29:
        t = tbt_id(it['l'])
        if not t: continue
        tbt[t] = it
        for h in claims(it['l']): h2tbt[h].add(t)
    ids = set(tbt)
    for it in I106:
        # ids come in runs: "TBT `4`/`6`", "delegated -> #29 `56`/`279`/`179`" - take them all
        named = set()
        for m in re.finditer(r'(?:#29|TBT)\s*((?:`\d+`\s*/?\s*)+)', it['l']):
            named |= set(re.findall(r'\d+', m.group(1)))
        # 1. ownership terminates
        if it['v'].startswith('delegated'):
            for t in named & ids:
                if re.search(r'#106[^.]{0,30}carries the checkbox|Ref only', tbt[t]['l']) and \
                   not tbt[t]['v'].startswith('drop'):
                    bad('relational', 'LOOP: #106 L%d delegates to TBT %s, which hands its verdict back'
                        % (it['i'], t))
            for t in named - ids:
                bad('relational', '#106 L%d: delegation target TBT %s has no #29 line' % (it['i'], t))
        # 2. a pointer is total   4. a counterpart names you back
        for h in set(re.findall(r'`([0-9a-f]{7,10})`', it['l'])):
            cl = {t for t in h2tbt.get(h, set()) if not tbt[t]['v'].startswith('drop')}
            miss = cl - named
            if miss and it['v'].startswith('delegated'):
                bad('relational', '#106 L%d: marker omits TBT %s, which also claims `%s`'
                    % (it['i'], sorted(miss, key=int), h))
            elif miss:
                bad('relational', '#106 L%d: `%s` is claimed by TBT %s, not named here'
                    % (it['i'], h, sorted(miss, key=int)))

    # ---- measured ---------------------------------------------------------
    if a.repo:
        R = a.repo
        git = lambda *c: subprocess.run(['git', '-C', R, *c], capture_output=True).stdout.decode('utf-8', 'replace')
        anc = lambda h, r: subprocess.run(['git', '-C', R, 'merge-base', '--is-ancestor', h, r],
                                          capture_output=True).returncode == 0
        seen = defaultdict(set)
        for t in (A, B):
            for h in set(re.findall(r'`([0-9a-f]{7,10})`', t)):
                if subprocess.run(['git', '-C', R, 'rev-parse', '-q', '--verify', h + '^{commit}'],
                                  capture_output=True).returncode:
                    bad('measured', 'citation `%s` resolves to no commit' % h); continue
                if not (anc(h, 'origin/main') or anc(h, 'origin/devel')):
                    bad('measured', 'citation `%s` is not reachable from main or devel (PR-branch sha?)' % h)
                seen[git('rev-parse', h + '^{commit}').strip()].add(h)
        for full, abbrevs in seen.items():
            if len(abbrevs) > 1:
                bad('measured', 'one commit cited under %s' % sorted(abbrevs))
        prs = {}
        try:
            prs = {p['number']: p for p in json.loads(subprocess.run(
                ['gh', 'pr', 'list', '--repo', a.slug, '--state', 'all', '--limit', '600',
                 '--json', 'number,state,isDraft'], capture_output=True, text=True).stdout or '[]')}
        except Exception: pass
        for src, II in (('29', I29), ('106', I106)):
            for it in II:
                scope = it['l'] + ' ' + it['head']
                if it['icon'] == '🟢' or 'drop — already in `main`' in it['v']:
                    ok = any(prs.get(int(p), {}).get('state') == 'MERGED' for p in re.findall(r'#(\d+)', scope)) \
                         or any(anc(h, 'origin/main') for h in re.findall(r'`([0-9a-f]{7,10})`', scope)) \
                         or re.search(r'\.(c|h|sh|cfg|rules|8|5)`?:\d+', scope)
                    if not ok:
                        bad('measured', '#%s L%d: in-main claim with no main-side evidence' % (src, it['i']))
                for p in re.findall(r'#(\d+)', it['l']):
                    if prs.get(int(p), {}).get('state') == 'CLOSED' and it['box'] != '-':
                        bad('measured', '#%s L%d: names #%s, closed unmerged - it carries nothing'
                            % (src, it['i'], p))

    # ---- group counts (a blank line closes the group) ---------------------
    for src, t in (('29', A), ('106', B)):
        L = t.split('\n')
        for i, l in enumerate(L):
            m = re.search(r'\*group of (\d+) (?:commits?|patches)', l)
            if not m: continue
            c, started = 0, False
            for s in L[i+1:]:
                if s.startswith('- '): c, started = c + 1, True
                elif started: break
                elif s.startswith('**'): break
            if c != int(m.group(1)):
                bad('structural', '#%s L%d: heading says %s members, found %d' % (src, i+1, m.group(1), c))

    for kind in ('structural', 'relational', 'measured'):
        n = [m for k, m in fails if k == kind]
        print("  %-11s %s" % (kind, 'PASS' if not n else 'FAIL (%d)' % len(n)))
        for m in n: print("     " + m)
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main())
