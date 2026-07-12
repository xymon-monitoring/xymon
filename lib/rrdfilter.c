/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Generic RRDEXCLUDE/RRDINCLUDE trending filter (issue #244). One implementation,    */
/* two consumers: xymond's RRD writer decides which files exist, and the      */
/* graph-paging line counter in htmllog.c mirrors the same decision - shared  */
/* code is what guarantees they can never disagree (the NORRDDISKS paging     */
/* divergence of issue #234 is exactly what happens when they do).            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "libxymon.h"
#include "rrdfilter.h"

/*
 * Generic trending filter (issue #244): RRDEXCLUDE and RRDINCLUDE hold
 * whitespace-separated "testname:regex" entries (whitespace, because the
 * instance names regexes must match are full of commas), matched against the RRD
 * filename with the ".rrd" suffix stripped - the instance name as it
 * appears in the host's RRD directory (e.g. "disk,poudriere,data3",
 * "iostat.da15"). A file matching a RRDEXCLUDE entry for its test is not
 * created or updated; when RRDINCLUDE has entries for a test, only matching
 * files are. The disk-family RRDDISKS/NORRDDISKS filters (do_disk.c,
 * matching raw mount points) are unchanged and evaluated first.
 * NOTE: for the disk family, prefer NORRDDISKS - the status-page graph
 * paging mirrors those patterns; RRDEXCLUDE does not feed that mirror (yet).
 */
typedef struct rrdfilter_t {
	char *testname;
	pcre2_code *pattern;
	struct rrdfilter_t *next;
} rrdfilter_t;
static rrdfilter_t *excludelist = NULL, *includelist = NULL;
static int rrdfiltersetup = 0;

static rrdfilter_t *parse_rrdfilter(char *envname)
{
	rrdfilter_t *head = NULL, *newitem;
	char *val, *tok, *saveptr, *ptn;

	val = getenv(envname);
	if (!val || !*val) return NULL;

	val = strdup(val);
	tok = strtok_r(val, " \t", &saveptr);
	while (tok) {
		ptn = strchr(tok, ':');
		if (ptn) {
			char errmsg[120];
			int err;
			PCRE2_SIZE errofs;
			pcre2_code *pat;

			*ptn = '\0'; ptn++;
			pat = pcre2_compile((PCRE2_SPTR)ptn, strlen(ptn), PCRE2_CASELESS, &err, &errofs, NULL);
			if (pat) {
				newitem = (rrdfilter_t *)calloc(1, sizeof(rrdfilter_t));
				newitem->testname = strdup(tok);
				newitem->pattern = pat;
				newitem->next = head;
				head = newitem;
			}
			else {
				pcre2_get_error_message(err, (PCRE2_UCHAR *)errmsg, sizeof(errmsg));
				errprintf("PCRE compile of %s entry '%s:%s' failed, error %s, offset %zu\n",
					  envname, tok, ptn, errmsg, errofs);
			}
		}
		else errprintf("Ignoring malformed %s entry '%s' (want testname:regex)\n", envname, tok);
		tok = strtok_r(NULL, " \t", &saveptr);
	}
	free(val);
	return head;
}

static int rrdfiltermatch(rrdfilter_t *item, char *instance)
{
	pcre2_match_data *ovector;
	int result;

	ovector = pcre2_match_data_create_from_pattern(item->pattern, NULL);
	result = pcre2_match(item->pattern, (PCRE2_SPTR)instance, strlen(instance), 0, 0, ovector, NULL);
	pcre2_match_data_free(ovector);
	return (result >= 0);
}

/* Returns 1 when RRDEXCLUDE/RRDINCLUDE say this file must not be tracked */
int rrd_is_filtered(char *testname, char *fn)
{
	rrdfilter_t *walk;
	char instance[PATH_MAX];
	char *p;
	int havetest;

	if (!rrdfiltersetup) {
		rrdfiltersetup = 1;
		excludelist = parse_rrdfilter("RRDEXCLUDE");
		includelist = parse_rrdfilter("RRDINCLUDE");
	}
	if (!excludelist && !includelist) return 0;

	snprintf(instance, sizeof(instance), "%s", fn);
	p = strrchr(instance, '.');
	if (p && (strcmp(p, ".rrd") == 0)) *p = '\0';

	for (walk = excludelist; (walk); walk = walk->next) {
		if ((strcmp(walk->testname, testname) == 0) && rrdfiltermatch(walk, instance)) return 1;
	}

	havetest = 0;
	for (walk = includelist; (walk); walk = walk->next) {
		if (strcmp(walk->testname, testname) != 0) continue;
		if (rrdfiltermatch(walk, instance)) return 0;
		havetest = 1;
	}
	return havetest;
}

