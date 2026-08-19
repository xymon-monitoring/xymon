/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for matching names and expressions                    */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "pcre2_api_compat.h"

#include "libxymon.h"

/*
 * As compileregex_opts(), but also hands back the pcre2 error code and offset
 * so a caller can react to *why* a pattern failed - the message itself is
 * still logged here, so callers only interested in that can keep using the
 * simpler wrappers below. Both out-parameters are optional (pass NULL).
 */
pcre2_code *compileregex_ext(const char *pattern, uint32_t flags, int *errcode, PCRE2_SIZE *erroffset)
{
	pcre2_code *result;
	char errmsg[120];
	int err;
	PCRE2_SIZE errofs;

	dbgprintf("Compiling regex %s\n", pattern);
	result = pcre2_compile(PCRE2STR(pattern), strlen(pattern), flags, &err, &errofs, NULL);
	if (result == NULL) {
		pcre2_get_error_message(err, PCRE2BUF(errmsg), sizeof(errmsg));
		errprintf("pcre compile '%s' failed (offset %zu): %s\n", pattern, errofs, errmsg);
		if (errcode) *errcode = err;
		if (erroffset) *erroffset = errofs;
		return NULL;
	}

	if (errcode) *errcode = 0;
	if (erroffset) *erroffset = 0;

	return result;
}

pcre2_code *compileregex_opts(const char *pattern, uint32_t flags)
{
	return compileregex_ext(pattern, flags, NULL, NULL);
}

pcre2_code *compileregex(const char *pattern)
{
	return compileregex_opts(pattern, PCRE2_CASELESS);
}

pcre2_code *multilineregex(const char *pattern)
{
	return compileregex_opts(pattern, PCRE2_CASELESS|PCRE2_MULTILINE);
}

int matchregex(const char *needle, pcre2_code *pcrecode)
{
	pcre2_match_data *ovector;
	int result;

	if (!needle || !pcrecode) return 0;

	ovector = pcre2_match_data_create_from_pattern(pcrecode, NULL);
	if (!ovector) return 0;
	result = pcre2_match(pcrecode, PCRE2STR(needle), strlen(needle), 0, 0, ovector, NULL);
	pcre2_match_data_free(ovector);
	return (result >= 0);
}

void freeregex(pcre2_code *pcrecode)
{
	if (!pcrecode) return;

	pcre2_code_free(pcrecode);
}

/*
 * pagepath_matchname() -- the name a pagepath is matched against.
 *
 * The top-level page has no pagepath of its own: it is the empty string, set on
 * the pagelist head in lib/loadhosts.c. What that costs depends on how the
 * filter compares:
 *
 *   namematch() refuses an empty needle outright, so alerts.cfg's PAGE= could
 *   not select the top-level page at all - which is why criteriamatch() has
 *   mapped "" to "/" by hand since 4abd6f3e0.
 *
 *   matchregex() and the raw pcre2_match() calls in the log filters refuse only
 *   NULL, so an empty subject is matchable: "^$" and ".*" both reached those
 *   hosts. What could not reach them was a pattern naming the page. So
 *   `xymondboard page=/` selected nothing while `page=^$` worked - the opposite
 *   of what analysis.cfg(5) tells an admin to write, and undocumented besides.
 *
 * Giving the page one name settles both. It is "/", which is what analysis.cfg(5)
 * documents and what criteriamatch() already used, so alerts.cfg does not change
 * meaning. The regex surfaces do: "^$" stops selecting the top page and "^/$"
 * starts. That is a break for a filter written against the old behaviour, and it
 * is deliberate - one name, the documented one, on every surface.
 *
 * Anchor a regex at both ends. These filters are unanchored, so "/" alone also
 * matches every pagepath containing a separator, exactly as "sub" matches
 * "subpage" - a property of the filter, not of this name.
 *
 * It lives here, beside namematch() and matchregex(), because how they treat an
 * empty needle is the whole of the problem - and because matching.o is in both
 * lib archives, while loadhosts.o is only in libxymoncomm.a.
 *
 * Apply it to the value being compared, not to the result of xmh_item_multi(),
 * which returns NULL to end its iteration and must keep doing so. Applied to a
 * comma-separated list it can only name a wholly empty one; an empty element
 * inside a list is unrepresentable once strtok() has dropped it, which is why
 * XMH_ALLPAGEPATHS has to emit the name itself (issue #526).
 *
 * The "/" returned for the top page is a string literal, as XMH_PAGEPATHTITLE's
 * "Top Page" is in xmh_item(): callers compare it, they do not write to it.
 */
char *pagepath_matchname(char *pagepath)
{
	return ((pagepath && *pagepath) ? pagepath : "/");
}

int namematch(const char *needle, char *haystack, pcre2_code *pcrecode)
{
	char *xhay;
	char *tokbuf = NULL, *tok;
	int found = 0;
	int result = 0;
	int allneg = 1;

	if ((needle == NULL) || (*needle == '\0')) return 0;

	if (pcrecode) {
		/* Do regex matching. The regex has already been compiled for us. */
		return matchregex(needle, pcrecode);
	}

	if (strcmp(haystack, "*") == 0) {
		/* Match anything */
		return 1;
	}

	/* Implement a simple, no-wildcard match */
	xhay = strdup(haystack);

	tok = strtok_r(xhay, ",", &tokbuf);
	while (tok) {
		allneg = (allneg && (*tok == '!'));

		if (!found) {
			if (*tok == '!') {
				found = (strcasecmp(tok+1, needle) == 0);
				if (found) result = 0;
			}
			else {
				found = (strcasecmp(tok, needle) == 0);
				if (found) result = 1;
			}
		}

		/* We must check all of the items in the haystack to see if they are all negative matches */
		tok = strtok_r(NULL, ",", &tokbuf);
	}
	xfree(xhay);

	/* 
	 * If we didn't find it, and the list is exclusively negative matches,
	 * we must return a positive result for "no match".
	 */
	if (!found && allneg) result = 1;

	return result;
}

int patternmatch(char *datatosearch, char *pattern, pcre2_code *pcrecode)
{
	if (pcrecode) {
		/* Do regex matching. The regex has already been compiled for us. */
		return matchregex(datatosearch, pcrecode);
	}

	if (strcmp(pattern, "*") == 0) {
		/* Match anything */
		return 1;
	}

	return (strstr(datatosearch, pattern) != NULL);
}

pcre2_code **compile_exprs(char *id, const char **patterns, int count)
{
	pcre2_code **result = NULL;
	int i;

	result = (pcre2_code **)calloc(count, sizeof(pcre2_code *));
	for (i=0; (i < count); i++) {
		result[i] = compileregex(patterns[i]);
		if (!result[i]) {
			errprintf("Internal error: %s pickdata PCRE-compile failed\n", id);
			for (i=0; (i < count); i++) if (result[i]) pcre2_code_free(result[i]);
			xfree(result);
			return NULL;
		}
	}

	return result;
}

/* nargs: how many char** destinations the caller passed -- NOT optional.
 * pcre2_match() returns 1 + (highest capture group that matched), a count that
 * depends on the DATA: an optional group fires only for some inputs. If it ever
 * exceeds the fixed argument list, the loop pulls a va_arg that was never passed
 * and dereferences a garbage pointer -- likely resulting in memory-unsafe read,
 * xfree() or write operation (issue #433: a Linux iface name with an underscore,
 * e.g. "veth_0", makes an ifstat pattern capture one group more than its call
 * site supplied). Clamp res to what the caller vouched for. */
int pickdata(char *buf, pcre2_code *expr, int dupok, int nargs, ...)
{
	int res, i;
	pcre2_match_data *ovector;
	va_list ap;
	char **ptr;
	char w[100];
	PCRE2_SIZE l;

	if (!expr) return 0;

	ovector = pcre2_match_data_create_from_pattern(expr, NULL);
	if (!ovector) return 0;

	res = pcre2_match(expr, PCRE2STR(buf), strlen(buf), 0, 0, ovector, NULL);
	if (res <= 0) {
		pcre2_match_data_free(ovector);
		return 0;
	}
	if (res > nargs + 1) res = nargs + 1; /* loop runs i = 1 .. res-1 */

	va_start(ap, nargs);

	for (i=1; (i < res); i++) {
		*w = '\0';
		l = sizeof(w);
		pcre2_substring_copy_bynumber(ovector, i, PCRE2BUF(w), &l);
		ptr = va_arg(ap, char **);
		if (dupok) {
			if (*ptr) xfree(*ptr);
			*ptr = strdup(w);
		}
		else {
			if (*ptr == NULL) {
				*ptr = strdup(w);
			}
			else {
				dbgprintf("Internal error: Duplicate match ignored\n");
			}
		}
	}

	va_end(ap);
	pcre2_match_data_free(ovector);

	return 1;
}


int timematch(char *holidaykey, char *tspec)
{
	int result;

	result = within_sla(holidaykey, tspec, 0);

	return result;
}
