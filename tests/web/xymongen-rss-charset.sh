#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/web/xymongen-rss-charset.sh
#
# xymongen's RSS/RDF feed (xymongen/rssgen.c) must declare its encoding and
# emit free text as well-formed XML in the pages' charset, as the appfeed and
# perfdata XML feeds do since #934/#942 -- part of #927. It used to write a
# bare "<?xml version='1.0'?>" (no encoding, so a non-UTF-8 charset is read as
# UTF-8 by the reader), escape only the <description> with its own &<> loop
# (not the <title> or the <link>'s query '&'), and never check the bytes are
# well-formed UTF-8.
#
# Driven on the shared status board (tests/lib/xymongen-board.sh): a red status
# whose text carries '&', '<', a valid UTF-8 character and an invalid byte goes
# into the non-green page's RSS with --rss. The feed must declare the charset,
# escape '&' and '<', keep the valid UTF-8 byte, and not carry the invalid one
# through raw.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"
# shellcheck source=tests/lib/xymongen-board.sh
. "$(dirname "$0")/../lib/xymongen-board.sh"

require_bin XYMONGEN xymongen/xymongen

board_setup

# Replace alpha.test's red cpu status text with one that exercises escaping
# (& <), a valid UTF-8 character (c3 a9 = U+00E9), an invalid UTF-8 byte (80, a
# lone continuation byte), and a raw Latin-1 byte (e9) -- invalid as UTF-8, a
# valid 'e' with acute in ISO-8859-1.
printf 'alpha.test|cpu|red||1791500883|1791500883|1791502683|0|0|127.0.0.1|819012|R&D caf\xc3\xa9 \xe9 \x80 <x>|\n' > "$work/cpu.line"
grep -v '^alpha\.test|cpu|' "$work/board" > "$work/board.rest"
cat "$work/cpu.line" "$work/board.rest" > "$work/board"

# --- UTF-8: the configured charset the feed is declared and validated in ------
board_render rss "HTMLCONTENTTYPE=text/html; charset=UTF-8" -- --rss
assert_equal "0" "$RC" "xymongen --rss failed: $(tail -c 300 "$work/rss.err")"

rss="$work/www/nongreen.rss"
assert_file_exists "$rss" "xymongen wrote no non-green RSS feed"

# 1. The feed declares its encoding (not a bare <?xml version="1.0"?>).
assert_contains 'encoding="UTF-8"' "$(head -1 "$rss")" \
	"the RSS feed does not declare its charset in the XML header"

# 2. The status text reached the <description>, with & and < escaped.
body=$(cat "$rss")
assert_contains "R&amp;D" "$body" "'&' in the status text was not escaped in the feed"
assert_contains "&lt;x&gt;" "$body" "'<'/'>' in the status text was not escaped in the feed"

# 3. The valid UTF-8 character is carried through unchanged (c3 a9).
LC_ALL=C grep -q "$(printf 'caf\xc3\xa9')" "$rss" \
	|| fail "the valid UTF-8 character was lost from the feed"

# 4. The invalid UTF-8 bytes (0x80, and the raw Latin-1 0xe9) are not carried
#    through raw: in a UTF-8 feed xmlquoted() replaces each with U+FFFD
#    (ef bf bd) so the feed stays well-formed.
if LC_ALL=C grep -q "$(printf '\x80')" "$rss"; then
	fail "the 0x80 byte reached the UTF-8 feed raw, leaving it not well-formed"
fi
if LC_ALL=C grep -q "$(printf '\xe9')" "$rss"; then
	fail "the raw Latin-1 0xe9 reached the UTF-8 feed raw, leaving it not well-formed"
fi

# 5. The dynamic <item> link carries the query '&' escaped exactly once. The
#    link is built by hostsvcurl() and then quoted by xmlquoted(); asking
#    hostsvcurl() for the HTML form ('&amp;') and quoting that again gave
#    '&amp;amp;', so the browser saw a parameter named "amp;SERVICE" and the
#    item opened no status page (regression this test pins).
assert_contains 'HOST=alpha.test&amp;SERVICE=cpu</link>' "$body" \
	"the <item> link does not carry the query separator escaped once"
if grep -q '&amp;amp;' "$rss"; then
	fail "the <item> link's '&' was escaped twice (&amp;amp;)"
fi
# Where xmllint is available, check the parsed link value directly.
if command -v xmllint >/dev/null 2>&1; then
	link=$(xmllint --xpath 'string(//item[1]/link)' "$rss" 2>/dev/null || true)
	assert_contains 'HOST=alpha.test&SERVICE=cpu' "$link" \
		"the parsed <item> link lost its SERVICE parameter"
fi

# --- ISO-8859-1: a non-UTF-8 charset is declared and its high bytes kept ------
board_render rssiso "HTMLCONTENTTYPE=text/html; charset=ISO-8859-1" -- --rss
assert_equal "0" "$RC" "xymongen --rss (ISO-8859-1) failed: $(tail -c 300 "$work/rssiso.err")"

assert_contains 'encoding="ISO-8859-1"' "$(head -1 "$rss")" \
	"the RSS feed does not follow the configured ISO-8859-1 charset in its header"
# In a non-UTF-8 feed the high byte is a valid character and is kept as is.
LC_ALL=C grep -q "$(printf '\xe9')" "$rss" \
	|| fail "the Latin-1 0xe9 byte was not kept in the ISO-8859-1 feed"
# The link is escaped once whatever the charset.
assert_contains 'HOST=alpha.test&amp;SERVICE=cpu</link>' "$(cat "$rss")" \
	"the <item> link regressed under ISO-8859-1"

pass "xymongen's RSS feed declares the configured charset, keeps its high bytes, and escapes free text and the item link's '&' exactly once"
