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

#include "pcre_compat.h"

#include "libxymon.h"

pcre_pattern_t *compileregex_opts(const char *pattern, int flags)
{
	pcre_pattern_t *result;
	char errmsg[256];
	int errofs;

	dbgprintf("Compiling regex %s\n", pattern);
	result = pcre_compile_compat(pattern, flags, errmsg, sizeof(errmsg), &errofs);
	if (result == NULL) {
		errprintf("pcre compile '%s' failed (offset %d): %s\n", pattern, errofs, errmsg);
		return NULL;
	}

	return result;
}

pcre_pattern_t *compileregex(const char *pattern)
{
	return compileregex_opts(pattern, PCRE_CASELESS);
}

pcre_pattern_t *multilineregex(const char *pattern)
{
	return compileregex_opts(pattern, PCRE_CASELESS|PCRE_MULTILINE);
}

int matchregex(const char *needle, pcre_pattern_t *pcrecode)
{
	int result;
	pcre_match_data_t *match_data;

	if (!needle || !pcrecode) return 0;

	match_data = pcre_match_data_create_compat(pcrecode);
	if (!match_data) return 0;

	result = pcre_exec_compat(pcrecode, needle, strlen(needle), match_data);
	pcre_match_data_free_compat(match_data);

	return (result >= 0);
}

void freeregex(pcre_pattern_t *pcrecode)
{
	if (!pcrecode) return;

	pcre_free_compat(pcrecode);
}

int namematch(const char *needle, char *haystack, pcre_pattern_t *pcrecode)
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
				found = (strcmp(tok+1, needle) == 0);
				if (found) result = 0;
			}
			else {
				found = (strcmp(tok, needle) == 0);
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

int patternmatch(char *datatosearch, char *pattern, pcre_pattern_t *pcrecode)
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

pcre_pattern_t **compile_exprs(char *id, const char **patterns, int count)
{
	pcre_pattern_t **result = NULL;
	int i;

	result = (pcre_pattern_t **)calloc(count, sizeof(pcre_pattern_t *));
	for (i=0; (i < count); i++) {
		result[i] = compileregex(patterns[i]);
		if (!result[i]) {
			errprintf("Internal error: %s pickdata PCRE-compile failed\n", id);
			for (i=0; (i < count); i++) if (result[i]) pcre_free_compat(result[i]);
			xfree(result);
			return NULL;
		}
	}

	return result;
}

int pickdata(char *buf, pcre_pattern_t *expr, int dupok, ...)
{
	int res, i;
	va_list ap;
	char **ptr;
	char w[100];
	pcre_match_data_t *match_data;

	if (!expr) return 0;

	match_data = pcre_match_data_create_compat(expr);
	if (!match_data) return 0;

	res = pcre_exec_compat(expr, buf, strlen(buf), match_data);
	if (res < 0) {
		pcre_match_data_free_compat(match_data);
		return 0;
	}

	va_start(ap, dupok);

	for (i=1; (i < res); i++) {
		*w = '\0';
		pcre_copy_substring_compat(buf, match_data, i, w, sizeof(w));
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
	pcre_match_data_free_compat(match_data);

	return 1;
}

int timematch(char *holidaykey, char *tspec)
{
	int result;

	result = within_sla(holidaykey, tspec, 0);

	return result;
}
