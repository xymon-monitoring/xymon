/* Trends-page membership from graphs.cfg: a gdef marked TRENDS appears on
 * the trends page without any GRAPHS env entry, with its MAXINSTANCESPERIMAGE applied
 * and per-file counts; INCLUDE variants inherit the membership; unmarked,
 * unlisted gdefs stay off the page. Drives the real generate_trends(). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libxymon.h"

extern char *generate_trends(char *hostname, time_t starttime, time_t endtime);

static int failures = 0;

static void expect(const char *label, const char *text, const char *needle, int want)
{
	int have = (text && strstr(text, needle) != NULL);

	if (have != want) {
		fprintf(stderr, "%s: '%s' %s in:\n%s\n", label, needle,
			(want ? "missing" : "unexpected"), (text ? text : "(null)"));
		failures++;
	}
}

int main(void)
{
	char *html;

	{
		/* "!" forces a file load - a bare $HOSTSCFG path detours
		 * through a xymond network query first */
		char hostsfn[1024];
		snprintf(hostsfn, sizeof(hostsfn), "!%s", getenv("HOSTSCFG"));
		load_hostnames(hostsfn, NULL, get_fqdn());
	}
	html = generate_trends("testhost", 0, 0);

	/* TRENDS gdef, not in GRAPHS: member, split by its MAXINSTANCESPERIMAGE 1
	 * (2 files -> 2 slices of 1) */
	expect("TRENDS gdef is a trends member", html, "service=markermetric&amp;", 1);
	expect("MAXINSTANCESPERIMAGE applies on the trends page", html, "first=1&amp;count=1", 1);
	expect("MAXINSTANCESPERIMAGE applies on the trends page", html, "first=2&amp;count=1", 1);

	/* INCLUDE variant inherits the base's TRENDS membership */
	expect("INCLUDE variant inherits TRENDS", html, "service=incvariant&amp;", 1);

	/* A gdef neither marked TRENDS nor listed in GRAPHS stays off */
	expect("unmarked gdef stays off the trends page", html, "othermetric", 0);

	printf(failures ? "FAILED\n" : "ALL OK\n");
	return failures ? 1 : 0;
}
