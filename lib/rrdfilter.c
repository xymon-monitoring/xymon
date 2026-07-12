/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Generic per-RRD-file RRDEXCLUDE/RRDINCLUDE trending filter (issue #244).   */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "libxymon.h"
#include "rrdfilter.h"

/*
 * RRDEXCLUDE and RRDINCLUDE hold whitespace-separated "scope:regex" entries.
 * The regex is matched against the RRD filename with ".rrd" stripped, i.e. the
 * file as it appears in the host's RRD directory. The scope matches either the
 * incoming Xymon test/column name, the RRD filename prefix before '.' or ',',
 * or "*" for all RRD files. Examples:
 *
 *   iostat:^iostat.da1[5-9]$
 *   ifstat:^ifstat.docker
 *   tcp:^tcp.http
 *   *:,tmp,
 *
 * A file matching RRDEXCLUDE is not created, updated, or selected for graph
 * display. When RRDINCLUDE has entries for a file's scope, only matching
 * files are trended and graphed. Exclude wins.
 */
typedef struct rrdfilter_t {
	char *scope;
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
				newitem->scope = strdup(tok);
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
		else errprintf("Ignoring malformed %s entry '%s' (want scope:regex)\n", envname, tok);
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

static void rrd_scope(char *scope, size_t scopelen, char *instance)
{
	char *p;

	snprintf(scope, scopelen, "%s", instance);
	p = scope + strcspn(scope, ".,");
	*p = '\0';
}

static int rrdfilterscope(rrdfilter_t *item, char *testname, char *rrdscope)
{
	if (strcmp(item->scope, "*") == 0) return 1;
	if (strcasecmp(item->scope, testname) == 0) return 1;
	return (strcasecmp(item->scope, rrdscope) == 0);
}

/* Returns 1 when RRDEXCLUDE/RRDINCLUDE say this file must not be tracked or graphed */
int rrd_is_filtered(char *testname, char *fn)
{
	rrdfilter_t *walk;
	char instance[PATH_MAX];
	char rrdscope[PATH_MAX];
	char *p;
	int havescope;

	if (!rrdfiltersetup) {
		rrdfiltersetup = 1;
		excludelist = parse_rrdfilter("RRDEXCLUDE");
		includelist = parse_rrdfilter("RRDINCLUDE");
	}
	if (!excludelist && !includelist) return 0;

	snprintf(instance, sizeof(instance), "%s", fn);
	p = strrchr(instance, '.');
	if (p && (strcmp(p, ".rrd") == 0)) *p = '\0';
	rrd_scope(rrdscope, sizeof(rrdscope), instance);

	for (walk = excludelist; (walk); walk = walk->next) {
		if (rrdfilterscope(walk, testname, rrdscope) && rrdfiltermatch(walk, instance)) return 1;
	}

	havescope = 0;
	for (walk = includelist; (walk); walk = walk->next) {
		if (!rrdfilterscope(walk, testname, rrdscope)) continue;
		if (rrdfiltermatch(walk, instance)) return 0;
		havescope = 1;
	}
	return havescope;
}
