/*----------------------------------------------------------------------------*/
/* Big Brother combination test tool.                                         */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbcombotest.c,v 1.9 2003-08-12 21:16:05 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "bbgen.h"
#include "debug.h"
#include "util.h"
#include "calc.h"
#include "sendmsg.h"

typedef struct value_t {
	char *symbol;
	int color;
	struct value_t *next;
} value_t;

typedef struct testspec_t {
	char *reshostname;
	char *restestname;
	char *expression;
	char *comment;
	char *resultexpr;
	value_t *valuelist;
	long result;
	struct testspec_t *next;
} testspec_t;

static testspec_t *testhead = NULL;
static int testcount = 0;

static char *gethname(char *spec)
{
	static char result[MAX_LINE_LEN];
	char *p;

	/* grab the hostname part from a "www.xxx.com.testname" string */
	strcpy(result, spec);
	p = strrchr(result, '.');
	if (p) *p = '\0';
	return result;
}

static char *gettname(char *spec)
{
	static char result[MAX_LINE_LEN];
	char *p;

	result[0] = '\0';

	/* grab the testname part from a "www.xxx.com.testname" string */
	p = strrchr(spec, '.');
	if (p) strcpy(result, p+1);

	return result;
}

static void loadtests(void)
{
	FILE *fd;
	char fn[MAX_PATH];
	char l[MAX_LINE_LEN];

	sprintf(fn, "%s/etc/bbcombotests.cfg", getenv("BBHOME"));
	fd = fopen(fn, "r");
	if (fd == NULL) return;

	while (fgets(l, sizeof(l), fd)) {
		char *p, *comment;

		/* Strip newline */
		p = strchr(l, '\n'); if (p) *p = '\0';

		/* Ignore empty lines and comment lines */
		for (p=l; (*p && isspace((int)*p)); p++) ;
		if (*p && (*p != '#')) {
			testspec_t *newtest = (testspec_t *) malloc(sizeof(testspec_t));

			p = strchr(l, '=');
			if (p) {
				*p = '\0';

				comment = strchr(p+1, '#');
				if (comment) *comment = '\0';
				newtest->reshostname = malcop(gethname(l));
				newtest->restestname = malcop(gettname(l));
				newtest->expression = malcop(p+1);
				newtest->comment = (comment ? malcop(comment+1) : NULL);
				newtest->resultexpr = NULL;
				newtest->valuelist = NULL;
				newtest->result = -1;
				newtest->next = testhead;
				testhead = newtest;
				testcount++;
			}

		}
	}

	fclose(fd);
}

static long getvalue(char *hostname, char *testname, int *color)
{
	char fn[MAX_PATH];
	FILE *fd;
	char l[MAX_LINE_LEN];
	struct stat st;
	testspec_t *walk;

	*color = -1;

	/* First check if it is one of our own tests */
	for (walk = testhead; (walk && ( (strcmp(walk->reshostname, hostname) != 0) || (strcmp(walk->restestname, testname) != 0) ) ); walk = walk->next);
	if (walk != NULL) {
		/* It is a combo test they want the result of. */
		return walk->result;
	}

	sprintf(fn, "%s/%s.%s", getenv("BBLOGS"), commafy(hostname), testname);
	if (stat(fn, &st)) {
		dprintf("No status file for host=%s, test=%s\n", hostname, testname);
	}
	else if (st.st_mtime < time(NULL)) {
		dprintf("Will not use a stale logfile for combo-tests - setting purple\n");
		*color = COL_PURPLE;
	}
	else {
		fd = fopen(fn, "r");
		if (fd == NULL) {
			dprintf("Cannot open file %s\n", fn);
			return -1;
		}

		if (fgets(l, sizeof(l), fd)) {
			*color = parse_color(l);
		}
		else {
			dprintf("Cannot read status file %s\n", fn);
		}

		fclose(fd);
	}

	if (*color == -1) return -1;
	else return ( (*color == COL_GREEN) || (*color == COL_YELLOW) || (*color == COL_CLEAR) );
}


static long evaluate(char *symbolicexpr, char **resultexpr, value_t **valuelist)
{
	char expr[MAX_LINE_LEN];
	char *inp, *outp, *symp;
	char symbol[MAX_LINE_LEN];
	int done;
	int insymbol = 0;
	int result, error;
	long oneval;
	int onecolor;
	value_t *valhead = NULL, *valtail = NULL;
	value_t *newval;

	done = 0; inp=symbolicexpr; outp=expr; symp = NULL; 
	while (!done) {
		if (isalpha((int)*inp)) {
			if (!insymbol) { insymbol = 1; symp = symbol; }
			*symp = *inp; symp++;
		}
		else if (insymbol && (isdigit((int) *inp) || (*inp == '.'))) {
			*symp = *inp; symp++;
		}
		else {
			if (insymbol) {
				/* Symbol finished - evaluate the symbol */
				*symp = '\0';
				insymbol = 0;
				oneval = getvalue(gethname(symbol), gettname(symbol), &onecolor);
				sprintf(outp, "%ld", oneval);
				outp += strlen(outp);

				newval = (value_t *) malloc(sizeof(value_t));
				newval->symbol = malcop(symbol);
				newval->color = onecolor;
				newval->next = NULL;
				if (valhead == NULL) {
					valtail = valhead = newval;
				}	
				else {
					valtail->next = newval;
					valtail = newval;
				}
			}

			*outp = *inp; outp++; symp = NULL;
		}

		if (*inp == '\0') done = 1; else inp++;
	}

	*outp = '\0';

	if (resultexpr) *resultexpr = malcop(expr);
	dprintf("Symbolic '%s' converted to '%s'\n", symbolicexpr, expr);

	error = 0; 
	result = compute(expr, &error);

	if (error) dprintf("calculate(%s) returned error %d\n", expr, error);
	*valuelist = valhead;
	return result;
}

/* These are dummy vars needed by stuff in util.c */
hostlist_t      *hosthead = NULL;
link_t          *linkhead = NULL;
link_t  null_link = { "", "", "", NULL };

char *reqenv[] = {
"BB",
"BBDISP",
"BBHOME",
"BBLOGS",
NULL };


int main(int argc, char *argv[])
{
	testspec_t *t;
	int argi, pending;
	int showeval = 1;

	for (argi = 1; (argi < argc); argi++) {
		if ((strcmp(argv[argi], "--help") == 0)) {
			printf("bbcombotest version %s\n\n", VERSION);
			printf("Usage:\n%s [--debug] [--quiet]\n", argv[0]);
			exit(0);
		}
		else if ((strcmp(argv[argi], "--version") == 0)) {
			printf("bbcombotest version %s\n", VERSION);
			exit(0);
		}
		else if ((strcmp(argv[argi], "--debug") == 0)) {
			debug = 1;
		}
		else if ((strcmp(argv[argi], "--quiet") == 0)) {
			showeval = 0;
		}
	}

	envcheck(reqenv);
	init_timestamp();
	loadtests();

	/*
	 * Loop over the tests to allow us "forward refs" in expressions.
	 * We continue for as long as progress is being made.
	 */
	do {
		pending = testcount;
		for (t=testhead; (t); t = t->next) {
			if (t->result == -1) {
				t->result = evaluate(t->expression, &t->resultexpr, &t->valuelist);
				if (t->result != -1) testcount--;
			}
		}

	} while (pending != testcount);

	combo_start();
	for (t=testhead; (t); t = t->next) {
		char msgline[MAX_LINE_LEN];
		int color;
		value_t *vwalk;

		color = (t->result ? COL_GREEN : COL_RED);
		init_status(color);
		sprintf(msgline, "status %s.%s %s %s\n\n", commafy(t->reshostname), t->restestname, colorname(color), timestamp);
		addtostatus(msgline);
		if (t->comment) { addtostatus(t->comment); addtostatus("\n\n"); }
		if (showeval) {
			sprintf(msgline, "%s = %s = %ld\n", t->expression, t->resultexpr, t->result);
			addtostatus(msgline);

			for (vwalk = t->valuelist; (vwalk); vwalk = vwalk->next) {
				sprintf(msgline, "&%s %s\n", colorname(vwalk->color), vwalk->symbol);
				addtostatus(msgline);
			}
		}
		finish_status();
	}
	combo_end();

	return 0;
}

