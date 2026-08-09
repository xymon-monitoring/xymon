/* Behavioural half of tests/server/xymond-noflap-list.sh.
 *
 * Guards the fix for hosts.cfg "noflap=test1,test2,..." silently losing
 * effect for every test after the first one evaluated.
 *
 * xmh_item() for XMH_NOFLAP returns a pointer straight INTO the host
 * record's own allelems buffer, not a copy (lib/loadhosts.c's XMH_NOFLAP
 * case falls through to xmh_find_item(), which returns
 * `host->elems[i] + keylen`; contrast XMH_DOCURL, which builds a copy).
 * xymond's isset_noflap() used to hand that pointer to strtok() directly,
 * which overwrote the list's first comma with a NUL and truncated the
 * stored tag permanently -- so every later status update saw only the first
 * test.
 *
 * The companion shell test prepends xymond/xymond.c's real static
 * isset_noflap() implementation when it builds the harness. This file adds
 * only the assertions that drive it against real host records loaded through
 * the real lib/loadhosts.c.
 *
 * The list assertions deliberately share one host and run in sequence: the
 * old defect only appeared on the SECOND and later evaluations, because the
 * first call still tokenized an intact string. That mirrors production,
 * where isset_noflap() runs once per incoming status message. A test using
 * a fresh host per test name would have passed even before the fix.
 */
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
	void *barehost, *listhost, *recordhost;
	char *before, *after;

	if (argc != 2) { fprintf(stderr, "usage: %s hosts.cfg\n", argv[0]); return 2; }

	if (load_hostnames(argv[1], NULL, 1) == -1) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 2;
	}

	barehost   = hostinfo("barehost.example.com");
	listhost   = hostinfo("listhost.example.com");
	recordhost = hostinfo("recordhost.example.com");
	if (!barehost || !listhost || !recordhost) {
		fprintf(stderr, "test hosts not found\n");
		return 2;
	}

	/* Bare flag form. lib/loadhosts.c's XMH_NOFLAP case mirrors flag
	 * semantics for it, so the value is the key string "NOFLAP" and
	 * isset_noflap() short-circuits before tokenizing anything. This path
	 * was never broken; assert it stayed that way. */
	expect("bare noflap: suppresses flapping for the first test queried",
	       isset_noflap(barehost, "web", "barehost") == 1,
	       "a bare noflap tag must disable flapping for every test on the host");
	expect("bare noflap: still suppresses on a second, different test",
	       isset_noflap(barehost, "cpu", "barehost") == 1,
	       "a bare noflap tag must keep working across repeated evaluations");

	/* The regression: every test in the list must stay suppressed no matter
	 * how many evaluations have already happened on this record. */
	expect("noflap list: first test evaluated is suppressed",
	       isset_noflap(listhost, "web", "listhost") == 1,
	       "web is listed in noflap=web,cpu,disk and must be suppressed");
	expect("noflap list: second test in the list is still suppressed",
	       isset_noflap(listhost, "cpu", "listhost") == 1,
	       "cpu is listed in noflap=web,cpu,disk and must still be suppressed after "
	       "an earlier evaluation of the same host");
	expect("noflap list: third test in the list is still suppressed",
	       isset_noflap(listhost, "disk", "listhost") == 1,
	       "disk is listed in noflap=web,cpu,disk and must still be suppressed after "
	       "two earlier evaluations of the same host");

	/* Control: an unlisted test must stay unsuppressed, so the assertions
	 * above cannot be satisfied by making the function always return 1. */
	expect("noflap list: an unlisted test is not suppressed",
	       isset_noflap(listhost, "mem", "listhost") == 0,
	       "mem is absent from noflap=web,cpu,disk and must not be suppressed");

	/* The root cause, stated directly: evaluating a host must leave its
	 * configuration record byte-for-byte intact. */
	before = strdup(xmh_item(recordhost, XMH_NOFLAP));
	(void)isset_noflap(recordhost, "web", "recordhost");
	after = xmh_item(recordhost, XMH_NOFLAP);
	expect("noflap: evaluating a host leaves its record intact",
	       after && (strcmp(before, after) == 0),
	       "isset_noflap() must not tokenize the pointer xmh_item() returned - it points "
	       "into the host record's own allelems buffer, so the stored tag gets modified");
	free(before);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
