/*----------------------------------------------------------------------------*/
/* Big Brother combination test tool.                                         */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbcombotest.c,v 1.22 2004-10-21 21:31:28 henrik Exp $";

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
	char *errbuf;
	struct testspec_t *next;
} testspec_t;

static testspec_t *testhead = NULL;
static int testcount = 0;
static int cleanexpr = 0;
static int usebbgend = 0;

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

	sprintf(fn, "%s/etc/bbcombotest.cfg", getenv("BBHOME"));
	fd = fopen(fn, "r");
	if (fd == NULL) {
		/* 
		 * Why this ? Because I goofed and released a version using bbcombotests.cfg,
		 * and you shouldn't break peoples' setups when fixing silly bugs.
		 */
		sprintf(fn, "%s/etc/bbcombotests.cfg", getenv("BBHOME"));
		fd = fopen(fn, "r");
	}
	if (fd == NULL) {
		errprintf("Cannot open %s/etc/bbcombotest.cfg\n", getenv("BBHOME"));
		return;
	}

	while (fgets(l, sizeof(l), fd)) {
		char *p, *comment;
		char *inp, *outp;

		p = strchr(l, '\n'); if (p) *p = '\0';
		/* Strip whitespace */
		for (inp=outp=l; ((*inp >= ' ') && (*inp != '#')); inp++) {
			if (isspace((int)*inp)) {
			}
			else {
				*outp = *inp;
				outp++;
			}
		}
		*outp = '\0';
		if (strlen(inp)) memmove(outp, inp, strlen(inp)+1);

		if (strlen(l) && (l[0] != '#') && (p = strchr(l, '=')) ) {
			testspec_t *newtest = (testspec_t *) malloc(sizeof(testspec_t));

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
			newtest->errbuf = NULL;
			newtest->next = testhead;
			testhead = newtest;
			testcount++;
		}
	}

	fclose(fd);
}

static int getfilevalue(char *hostname, char *testname, char **errptr)
{
	char fn[MAX_PATH];
	FILE *fd;
	char l[MAX_LINE_LEN];
	struct stat st;
	int statres;
	int result;

	sprintf(fn, "%s/%s.%s", getenv("BBLOGS"), commafy(hostname), testname);
	statres = stat(fn, &st);
	if (statres) {
		/* No file ? Maybe it is using the wrong (non-commafied) hostname */
		sprintf(fn, "%s/%s.%s", getenv("BBLOGS"), hostname, testname);
		statres = stat(fn, &st);
	}
	if (statres) {
		*errptr += sprintf(*errptr, "No status file for host=%s, test=%s\n", hostname, testname);
		result = COL_CLEAR;
	}
	else if (st.st_mtime < time(NULL)) {
		dprintf("Will not use a stale logfile for combo-tests - setting purple\n");
		result = COL_PURPLE;
	}
	else {
		fd = fopen(fn, "r");
		if (fd == NULL) {
			*errptr += sprintf(*errptr, "Cannot open file %s\n", fn);
		}
		else {
			if (fgets(l, sizeof(l), fd)) {
				result = parse_color(l);
			}
			else {
				*errptr += sprintf(*errptr, "Cannot read status file %s\n", fn);
			}
	
			fclose(fd);
		}
	}

	return result;
}

static int getbbgendvalue(char *hostname, char *testname, char **errptr)
{
	static char *board = NULL;
	int bbgendresult;
	int result;
	char *pattern, *found, *colstr, *p;

	if (board == NULL) {
		bbgendresult = sendmessage("bbgendboard", NULL, NULL, &board, 1);
		if (bbgendresult != BB_OK) {
			board = "";
			*errptr += sprintf(*errptr, "Could not access bbgend board, error %d\n", bbgendresult);
			return COL_CLEAR;
		}
	}

	pattern = (char *)malloc(1 + strlen(hostname) + 1 + strlen(testname) + 1 + 1);
	sprintf(pattern, "\n%s|%s|", hostname, testname);

	if (strncmp(board, pattern+1, strlen(pattern+1)) == 0) {
		/* The first entry in the board doesn't have the "\n" */
		found = board;
	}
	else {
		found = strstr(board, pattern);
	}

	if (found) {
		/* hostname|testname|color|testflags|logtime|lastchange|expires|sender|1st line of message */
		colstr = found + strlen(pattern);
		p = strchr(colstr, '|');
		if (p) {
			*p = '\0';
			result = parse_color(colstr);
			*p = '|';
		}
		else {
			*errptr += sprintf(*errptr, "Malformed board\n");
			found = NULL;
		}
	}

	if (!found) result = COL_CLEAR;

	free(pattern);
	return result;
}

static long getvalue(char *hostname, char *testname, int *color, char **errbuf)
{
	testspec_t *walk;
	char errtext[1024];
	char *errptr;
	int result;

	*color = -1;
	errptr = errtext; 
	*errptr = '\0';

	/* First check if it is one of our own tests */
	for (walk = testhead; (walk && ( (strcmp(walk->reshostname, hostname) != 0) || (strcmp(walk->restestname, testname) != 0) ) ); walk = walk->next);
	if (walk != NULL) {
		/* It is a combo test they want the result of. */
		return walk->result;
	}

	*color = (usebbgend ? getbbgendvalue(hostname, testname, &errptr) : getfilevalue(hostname, testname, &errptr));

	/* Save error messages */
	if (strlen(errtext) > 0) {
		if (*errbuf == NULL)
			*errbuf = malcop(errtext);
		else {
			*errbuf = (char *)realloc(*errbuf, strlen(*errbuf)+strlen(errtext)+1);
			strcat(*errbuf, errtext);
		}
	}

	if (*color == -1) return -1;
	else return ( (*color == COL_GREEN) || (*color == COL_YELLOW) || (*color == COL_CLEAR) );
}


static long evaluate(char *symbolicexpr, char **resultexpr, value_t **valuelist, char **errbuf)
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
	char errtext[1024];

	done = 0; inp=symbolicexpr; outp=expr; symp = NULL; 
	while (!done) {
		if (isalpha((int)*inp)) {
			if (!insymbol) { insymbol = 1; symp = symbol; }
			*symp = *inp; symp++;
		}
		else if (insymbol && (isdigit((int) *inp) || (*inp == '.'))) {
			*symp = *inp; symp++;
		}
		else if (insymbol && ((*inp == '\\') && (*(inp+1) > ' '))) {
			*symp = *(inp+1); symp++; inp++;
		}
		else {
			if (insymbol) {
				/* Symbol finished - evaluate the symbol */
				*symp = '\0';
				insymbol = 0;
				oneval = getvalue(gethname(symbol), gettname(symbol), &onecolor, errbuf);
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

	if (error) {
		sprintf(errtext, "compute(%s) returned error %d\n", expr, error);
		if (*errbuf == NULL) {
			*errbuf = malcop(errtext);
		}
		else {
			*errbuf = (char *)realloc(*errbuf, strlen(*errbuf)+strlen(errtext)+1);
			strcat(*errbuf, errtext);
		}
	}

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
"BBTMP",
NULL };


char *printify(char *exp)
{
	static char result[MAX_LINE_LEN];
	char *inp, *outp;
	size_t n;

	if (!cleanexpr) {
		return exp;
	}

	inp = exp;
	outp = result;

	while (*inp) {
		n = strcspn(inp, "|&");
		memcpy(outp, inp, n);
		inp += n; outp += n;

		if (*inp == '|') { 
			inp++;
			if (*inp == '|') {
				inp++;
				strcpy(outp, " OR "); outp += 4; 
			}
			else {
				strcpy(outp, " bOR "); outp += 5; 
			}
		}
		else if (*inp == '&') { 
			inp++; 
			if (*inp == '&') {
				inp++;
				strcpy(outp, " AND "); outp += 5; 
			}
			else {
				strcpy(outp, " bAND "); outp += 6;
			}
		}
	}

	*outp = '\0';
	return result;
}

int main(int argc, char *argv[])
{
	testspec_t *t;
	int argi, pending;
	int showeval = 1;

	setup_signalhandler("bbcombotest");

	for (argi = 1; (argi < argc); argi++) {
		if ((strcmp(argv[argi], "--help") == 0)) {
			printf("bbcombotest version %s\n\n", VERSION);
			printf("Usage:\n%s [--quiet] [--clean] [--debug] [--no-update]\n", argv[0]);
			exit(0);
		}
		else if ((strcmp(argv[argi], "--version") == 0)) {
			printf("bbcombotest version %s\n", VERSION);
			exit(0);
		}
		else if ((strcmp(argv[argi], "--debug") == 0)) {
			debug = 1;
		}
		else if ((strcmp(argv[argi], "--no-update") == 0)) {
			dontsendmessages = 1;
		}
		else if ((strcmp(argv[argi], "--quiet") == 0)) {
			showeval = 0;
		}
		else if ((strcmp(argv[argi], "--clean") == 0)) {
			cleanexpr = 1;
		}
		else if ((strcmp(argv[argi], "--bbgend") == 0)) {
			usebbgend = 1;
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
				t->result = evaluate(t->expression, &t->resultexpr, &t->valuelist, &t->errbuf);
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
			addtostatus(printify(t->expression));
			addtostatus(" = ");
			addtostatus(printify(t->resultexpr));
			addtostatus(" = ");
			sprintf(msgline, "%ld\n", t->result);
			addtostatus(msgline);

			for (vwalk = t->valuelist; (vwalk); vwalk = vwalk->next) {
				sprintf(msgline, "&%s %s\n", colorname(vwalk->color), vwalk->symbol);
				addtostatus(msgline);
			}

			if (t->errbuf) {
				addtostatus("\nErrors occurred during evaluation:\n");
				addtostatus(t->errbuf);
			}
		}
		finish_status();
	}
	combo_end();

	return 0;
}

