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

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "libxymon.h"

pcre2_code *compileregex_opts(const char *pattern, uint32_t flags)
{
	pcre2_code *result;
	char errmsg[120];
	int err;
	PCRE2_SIZE errofs;

	dbgprintf("Compiling regex %s\n", pattern);
	result = pcre2_compile(pattern, strlen(pattern), flags, &err, &errofs, NULL);
	if (result == NULL) {
		pcre2_get_error_message(err, errmsg, sizeof(errmsg));
		errprintf("pcre compile '%s' failed (offset %zu): %s\n", pattern, errofs, errmsg);
		return NULL;
	}

	return result;
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

	ovector = pcre2_match_data_create(30, NULL);
	result = pcre2_match(pcrecode, needle, strlen(needle), 0, 0, ovector, NULL);
	pcre2_match_data_free(ovector);
	return (result >= 0);
}

void freeregex(pcre2_code *pcrecode)
{
	if (!pcrecode) return;

	pcre2_code_free(pcrecode);
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

int pickdata(char *buf, pcre2_code *expr, int dupok, ...)
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

	res = pcre2_match(expr, buf, strlen(buf), 0, 0, ovector, NULL);
	if (res <= 0) {
		pcre2_match_data_free(ovector);
		return 0;
	}

	va_start(ap, dupok);

	for (i=1; (i < res); i++) {
		*w = '\0';
		l = sizeof(w);
		pcre2_substring_copy_bynumber(ovector, i, w, &l);
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

