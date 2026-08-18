/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Driver for the real generate_wml_statuscard(), extracted from
 * xymongen/wmlgen.c by the test beside this file.
 *
 * The function is static and sits on top of xymongen's whole object graph, so
 * what it needs is stubbed rather than linked: the daemon exchange returns a
 * canned log, and the types carry only the fields the function reads. What is
 * NOT stubbed is the function itself -- it is the production text, so a fix
 * that stops holding has nowhere to hide.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>

#define MAX_LINE_LEN 16384
#define XYMON_TIMEOUT 30
#define XYMONSEND_OK 0

typedef struct column_t { char *name; } column_t;
typedef struct entry_t { column_t *column; int color; int acked; struct entry_t *next; } entry_t;
typedef struct host_t { char *hostname; } host_t;
typedef struct sendreturn_t { int dummy; } sendreturn_t;

static char *wmldir;
static char *timestamp = "Mon Jan  1 00:00:00 2026";

/* The canned log: a header line, then one body line the parser copies out. */
static const char *CANNED = "green Mon Jan  1 00:00:00 2026\nplain body line\n";

static sendreturn_t *newsendreturnbuf(int a, void *b) { (void)a; (void)b; return (sendreturn_t *)calloc(1, sizeof(sendreturn_t)); }
static char *expected_req;	/* what this call must have been handed */
static int req_mismatch;

static int sendmessage(char *req, void *a, int b, sendreturn_t *c)
{
	(void)a; (void)b; (void)c;
	/*
	 * The whole request, not its prefix: a "fix" that bounded this by
	 * truncating the names would stop overflowing and still ask the daemon
	 * about the wrong test, which a prefix check cannot tell apart.
	 */
	if (expected_req && (strcmp(req, expected_req) != 0)) {
		fprintf(stderr, "FAIL: request was %zu bytes, expected %zu\n",
			strlen(req), strlen(expected_req));
		req_mismatch = 1;
	}
	return XYMONSEND_OK;
}

static void expect_req(const char *host, const char *col)
{
	free(expected_req);
	expected_req = (char *)malloc(strlen(host) + strlen(col) + sizeof("xymondlog ."));
	sprintf(expected_req, "xymondlog %s.%s", host, col);
}
static int empty_answer;	/* xymond has no log for this test */
static char *getsendreturnstr(sendreturn_t *s, int a)
{
	(void)s; (void)a;
	/* Still an allocation: "no status" is an empty string, not a NULL, which
	   is exactly the case whose early return used to drop it. */
	return strdup(empty_answer ? "" : CANNED);
}
static void freesendreturnbuf(sendreturn_t *s) { free(s); }
static void wml_header(FILE *fd, const char *name, int n) { (void)name; (void)n; fprintf(fd, "<wml>\n"); }
static void errprintf(const char *fmt, ...) { (void)fmt; }
static void *xmalloc(size_t n) { void *p = malloc(n); if (!p) abort(); return p; }
static void xfree(void *p) { free(p); }

#include "wmlgen-statuscard.inc"

static entry_t *mkentry(const char *col)
{
	entry_t *e = (entry_t *)calloc(1, sizeof(entry_t));
	e->column = (column_t *)calloc(1, sizeof(column_t));
	e->column->name = strdup(col);
	return e;
}

static char *repeat(char c, size_t n)
{
	char *s = (char *)malloc(n + 1);
	memset(s, c, n); s[n] = '\0';
	return s;
}

int main(int argc, char **argv)
{
	host_t host;
	entry_t *e;
	int rc, fail = 0;

	/* The test builds the deep directory from this. Reporting it from here
	   rather than from getconf(1) keeps it the same constant the function
	   under test is compiled against -- the buffers it fills are char[PATH_MAX]
	   -- and getconf reports a per-filesystem pathconf(), which need not
	   agree with the header. */
	if ((argc == 2) && (strcmp(argv[1], "--pathmax") == 0)) { printf("%d\n", PATH_MAX); return 0; }

	if (argc < 3) { fprintf(stderr, "usage: %s <wmldir> <deep-wmldir>\n", argv[0]); return 2; }
	wmldir = argv[1];

	/* 1. An ordinary card is written, and says so. */
	char *hn;

	hn = strdup("short.host");
	host.hostname = hn;
	e = mkentry("conn");
	expect_req(hn, "conn");
	rc = generate_wml_statuscard(&host, e);
	if (rc != 1) { fprintf(stderr, "FAIL: an ordinary status card reported failure (%d)\n", rc); fail = 1; }
	free(hn);

	/* 2. A hostname far past the old 1 KiB request buffer. Nothing here
	   asserts a value: the assertion is that ASan sees no write past the
	   buffer the request is built in, which happens before any path check. */
	hn = repeat('h', 1200);
	host.hostname = hn;
	expect_req(hn, "conn");
	rc = generate_wml_statuscard(&host, e);
	if (rc != 0) { fprintf(stderr, "NOTE: the 1200-byte name produced a card (%d)\n", rc); }
	free(hn);

	/* 3. The boundary that matters to the caller: the host card's own path
	   fits, and the status card's -- one component longer -- does not. The
	   function must report that, so the caller can stop advertising it. */
	{
		/* The deep directory, so every component stays inside NAME_MAX and it
		   is the *path* length that decides. With one huge component instead,
		   fopen() fails with ENAMETOOLONG and the case proves nothing about
		   the guard. */
		size_t room;
		char *name;
		char hostfn[PATH_MAX];
		int n;

		wmldir = argv[2];
		room = PATH_MAX - strlen(wmldir) - strlen("/") - strlen(".wml") - 1;
		if (room > 60) room = 60;              /* a plain name, well inside NAME_MAX */
		name = repeat('n', room);
		n = snprintf(hostfn, sizeof(hostfn), "%s/%s.wml", wmldir, name);

		if ((n < 0) || (n >= (int)sizeof(hostfn))) {
			fprintf(stderr, "FAIL: the host-card path was meant to fit, and does not (%d)\n", n);
			fail = 1;
		}
		host.hostname = name;
		expect_req(name, "conn");
		rc = generate_wml_statuscard(&host, e);   /* adds ".<column>" -- cannot fit */
		if (rc != 0) {
			fprintf(stderr, "FAIL: a status-card path that does not fit was reported as written (%d)\n", rc);
			fail = 1;
		}
		free(name);
	}

	/* 4. A status xymond has no log for. The answer is an empty string rather
	   than NULL -- an allocation the caller owns -- and the early return has
	   to release it. Nothing here asserts a value; LeakSanitizer does. */
	hn = strdup("nolog.host");
	host.hostname = hn;
	expect_req(hn, "conn");
	empty_answer = 1;
	rc = generate_wml_statuscard(&host, e);
	empty_answer = 0;
	if (rc != 0) { fprintf(stderr, "FAIL: a card with no status was reported as written (%d)\n", rc); fail = 1; }
	free(hn);

	/* 5. A write that cannot finish. RLIMIT_FSIZE makes every write past the
	   limit fail (SIGXFSZ ignored so the failure comes back as an error, not
	   a signal), which is where a full filesystem surfaces: at fprintf and
	   fclose, not at fopen. A card written short must not be reported as
	   written, or the caller links to a truncated one. */
	{
		struct rlimit rl;

		signal(SIGXFSZ, SIG_IGN);
		/* The soft limit only. Lowering the hard one is a one-way door for
		   an unprivileged process, and it would cap what the sanitizer can
		   write for the rest of the run -- its report came out truncated. */
		if (getrlimit(RLIMIT_FSIZE, &rl) != 0) rl.rlim_max = RLIM_INFINITY;
		rl.rlim_cur = 64;
		if (setrlimit(RLIMIT_FSIZE, &rl) == 0) {
			hn = strdup("shortwrite.host");
			host.hostname = hn;
			expect_req(hn, "conn");
			rc = generate_wml_statuscard(&host, e);
			if (rc != 0) {
				fprintf(stderr, "FAIL: a card whose write could not finish was reported as written (%d)\n", rc);
				fail = 1;
			}
			free(hn);
			rl.rlim_cur = rl.rlim_max;
			setrlimit(RLIMIT_FSIZE, &rl);
		}
		else fprintf(stderr, "note: RLIMIT_FSIZE unavailable, the short-write case is unverified\n");
	}

	free(e->column->name); free(e->column); free(e);
	free(expected_req);
	if (req_mismatch) { fprintf(stderr, "FAIL: the daemon request did not carry the names whole\n"); fail = 1; }

	if (!fail) printf("OK\n");
	return fail;
}
