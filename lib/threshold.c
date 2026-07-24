/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* threshold_eval - value vs declared threshold -> severity. See threshold.h. */
/* Self-contained (no Xymon deps) so it can be unit-tested by compiling this   */
/* file directly, and reused from lib, the web CGIs and xymond alike.          */
/*                                                                            */
/* Copyright (C) 2026 Bruno Manzoni                                           */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdlib.h>
#include <string.h>

#include "threshold.h"

/* Is "a <relop> b" true? relop is the `rlen`-char operator at `op`. */
static int relop_true(double a, const char *op, int rlen, double b)
{
	if (rlen == 1) {
		if (*op == '>') return (a > b);
		if (*op == '<') return (a < b);
	}
	else if ((rlen == 2) && (op[1] == '=')) {
		if (*op == '>') return (a >= b);
		if (*op == '<') return (a <= b);
	}
	return 0;
}

int threshold_eval(const char *thrspec, const char *dsname, double value,
		   int (*getval)(const char *ds, double *out, void *ud), void *ud)
{
	int worst = THRESHOLD_OK;
	size_t namelen;
	const char *rule;

	if (!thrspec || !dsname) return THRESHOLD_OK;
	namelen = strlen(dsname);

	/* Walk the comma-separated rules; each is "base:relopoperand[:sev]". */
	for (rule = thrspec; (rule && *rule); ) {
		const char *comma = strchr(rule, ',');
		const char *rend  = comma ? comma : (rule + strlen(rule));
		const char *c1    = memchr(rule, ':', rend - rule);

		if (c1 && ((size_t)(c1 - rule) == namelen) &&
		    (strncmp(rule, dsname, namelen) == 0)) {
			const char *mid    = c1 + 1;			/* relop + operand */
			const char *c2     = memchr(mid, ':', rend - mid);
			const char *midend = c2 ? c2 : rend;
			const char *op     = mid;
			int rlen = 0;
			int sev  = THRESHOLD_CRIT;			/* default when omitted */

			if (c2 && ((size_t)(rend - (c2 + 1)) == 4) &&
			    (strncmp(c2 + 1, "warn", 4) == 0)) sev = THRESHOLD_WARN;

			while ((op < midend) && ((*op == '>') || (*op == '<') || (*op == '='))) { op++; rlen++; }

			if ((rlen >= 1) && (op < midend)) {
				size_t oplen = midend - op;
				char operand[256];

				if (oplen < sizeof(operand)) {
					char *endp;
					double opval;
					int haveval;

					memcpy(operand, op, oplen); operand[oplen] = '\0';
					opval   = strtod(operand, &endp);
					haveval = ((endp != operand) && (*endp == '\0'));
					if (!haveval && getval) haveval = getval(operand, &opval, ud);

					if (haveval && relop_true(value, mid, rlen, opval) && (sev > worst))
						worst = sev;
				}
			}
		}

		rule = comma ? (comma + 1) : NULL;
	}

	return worst;
}
