/* Regression test for the XMH_ item-name table in lib/loadhosts.c.
 *
 * xmh_item_name[] holds the public name of each host attribute. Those names
 * are not internal labels - they are the field names clients use in
 * xymondboard/hostinfo requests (xymond/xymond.c), the names xymond emits in
 * hostinfo responses and lib/loadhosts_net.c parses back, and what web
 * templates resolve via xmh_item_byname() (lib/headfoot.c). They are
 * documented in xymon-xmh(5).
 *
 * XMH_FLAG_MULTIHOMED was registered as "XMH_MULTIHOMED", missing the FLAG_
 * that its enum name and xymon-xmh(5) both carry. Two consequences:
 *   - the documented name XMH_FLAG_MULTIHOMED did not resolve, so a
 *     board/hostinfo field selector or template reference using it was
 *     silently rejected;
 *   - xmh_item_isflag[] is derived by prefix-matching "XMH_FLAG_" against
 *     the registered name, so MULTIHOMED was never treated as a flag and
 *     xmh_item() returned the empty remainder after the key instead of the
 *     canonical key string that every other flag yields.
 *
 * Everything here drives the real lib/loadhosts.c through its public
 * interface - no mirrored logic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static int failures = 0;

static void expect(const char *label, int cond, const char *detail)
{
	if (!cond) {
		fprintf(stderr, "%s: %s\n", label, detail);
		failures++;
	}
}

int main(int argc, char *argv[])
{
	void *taghost, *plainhost;
	enum xmh_item_t idx;
	char *v, *name;

	if (argc != 2) { fprintf(stderr, "usage: %s hosts.cfg\n", argv[0]); return 2; }

	if (load_hostnames(argv[1], NULL, 1) == -1) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 2;
	}

	taghost   = hostinfo("taghost.example.com");    /* dialup MULTIHOMED */
	plainhost = hostinfo("plainhost.example.com");  /* neither tag */
	if (!taghost || !plainhost) { fprintf(stderr, "test hosts not found\n"); return 2; }

	/* The documented name must resolve to its own enum value. */
	expect("XMH_FLAG_MULTIHOMED resolves by its documented name",
	       xmh_key_idx("XMH_FLAG_MULTIHOMED") == XMH_FLAG_MULTIHOMED,
	       "xymon-xmh(5) documents XMH_FLAG_MULTIHOMED, so xmh_key_idx() must resolve it "
	       "rather than returning XMH_LAST");
	expect("XMH_FLAG_MULTIHOMED reports its documented name",
	       (name = xmh_item_id(XMH_FLAG_MULTIHOMED)) && (strcmp(name, "XMH_FLAG_MULTIHOMED") == 0),
	       "xmh_item_id() is what xymond emits in hostinfo responses, so it must match "
	       "the documented name");

	/* Flag semantics: a present flag yields its canonical key string, an
	 * absent one yields NULL. dialup is the control - a flag that was always
	 * registered correctly. */
	v = xmh_item(taghost, XMH_FLAG_DIALUP);
	expect("control: dialup flag yields its canonical key", v && (strcmp(v, "dialup") == 0),
	       "a correctly registered flag returns its key string");
	v = xmh_item(taghost, XMH_FLAG_MULTIHOMED);
	expect("MULTIHOMED flag yields its canonical key", v && (strcmp(v, "MULTIHOMED") == 0),
	       "MULTIHOMED must behave like every other flag and return its key string, not "
	       "the empty remainder after the key");

	/* The one in-tree consumer (xymond/xymond.c, suppressing the
	 * multiple-source-IP warning) tests the result against NULL. Absent must
	 * stay NULL, present must stay non-NULL. */
	expect("MULTIHOMED absent reads as NULL",
	       xmh_item(plainhost, XMH_FLAG_MULTIHOMED) == NULL,
	       "a host without the tag must read NULL so the == NULL consumer keeps working");
	expect("MULTIHOMED present reads as non-NULL",
	       xmh_item(taghost, XMH_FLAG_MULTIHOMED) != NULL,
	       "a host with the tag must read non-NULL so the == NULL consumer keeps working");

	/* Table-wide invariant: every registered name must resolve back to the
	 * entry that registered it, catching duplicated or colliding names. Note
	 * this does NOT catch a misnamed entry - "XMH_MULTIHOMED" round-tripped
	 * to its own slot perfectly well. The companion source check in the shell
	 * test is what catches that. */
	for (idx = 0; idx < XMH_LAST; idx++) {
		char detail[256];
		enum xmh_item_t back;

		name = xmh_item_id(idx);
		if (!name) continue;	/* not every slot is a named item */

		back = xmh_key_idx(name);
		if (back != idx) {
			snprintf(detail, sizeof(detail),
				 "xmh_item_id(%d) is \"%s\", but xmh_key_idx(\"%s\") gives %d%s",
				 (int)idx, name, name, (int)back,
				 (back == XMH_LAST) ? " (XMH_LAST - unresolvable)" : " (another entry)");
			expect("every XMH_ name round-trips to its own entry", 0, detail);
		}
	}

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
