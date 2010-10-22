/*----------------------------------------------------------------------------*/
/* Hobbit combination test tool.                                              */
/*                                                                            */
/* Copyright (C) 2003-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "version.h"
#include "libbbgen.h"

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

static char *gethname(char *spec)
{
	static char *result = NULL;
	char *p;

	if (result) xfree(result);

	/* grab the hostname part from a "www.xxx.com.testname" string */
	p = strrchr(spec, '.');
	if (!p) {
		errprintf("Item '%s' has no testname part\n", spec);
		return NULL;
	}

	*p = '\0'; 
	result = strdup(spec);
	*p = '.';

	return result;
}

static char *gettname(char *spec)
{
	static char *result = NULL;
	char *p;

	if (result) xfree(result);

	/* grab the testname part from a "www.xxx.com.testname" string */
	p = strrchr(spec, '.');
	if (!p) {
		errprintf("Item '%s' has no testname part\n", spec);
		return NULL;
	}
	result = strdup(p+1);

	return result;
}

static void flush_valuelist(value_t *head)
{
	value_t *walk, *zombie;

	walk = head;
	while (walk) {
		zombie = walk; walk = walk->next;
		xfree(zombie->symbol);
		xfree(zombie);
	}
}

static void flush_testlist(void)
{
	testspec_t *walk, *zombie;

	walk = testhead;
	while (walk) {
		zombie = walk; walk = walk->next;
		if (zombie->reshostname) xfree(zombie->reshostname);
		if (zombie->restestname) xfree(zombie->restestname);
		if (zombie->expression) xfree(zombie->expression);
		if (zombie->comment) xfree(zombie->comment);
		if (zombie->resultexpr) xfree(zombie->resultexpr);
		if (zombie->errbuf) xfree(zombie->errbuf);
		flush_valuelist(zombie->valuelist);
		xfree(zombie);
	}
	testhead = NULL;
	testcount = 0;
}

static void loadtests(void)
{
	static time_t lastupdate = 0;
	static char *fn = NULL;
	struct stat st;
	FILE *fd;
	strbuffer_t *inbuf;

	if (!fn) {
		fn = (char *)malloc(1024 + strlen(xgetenv("BBHOME")));
		*fn = '\0';
	}

	if (*fn == '\0') {
		/* 
		 * Why this ? Because I goofed and released a version using bbcombotests.cfg,
		 * and you shouldn't break peoples' setups when fixing silly bugs.
		 */
		sprintf(fn, "%s/etc/bbcombotest.cfg", xgetenv("BBHOME"));
		if (stat(fn, &st) == -1) sprintf(fn, "%s/etc/bbcombotests.cfg", xgetenv("BBHOME"));
	}
	if ((stat(fn, &st) == 0) && (st.st_mtime == lastupdate)) return;
	lastupdate = st.st_mtime;

	fd = fopen(fn, "r");
	if (fd == NULL) {
		errprintf("Cannot open %s/bbcombotest.cfg\n", xgetenv("BBHOME"));
		*fn = '\0';
		return;
	}

	flush_testlist();

	initfgets(fd);
	inbuf = newstrbuffer(0);
	while (unlimfgets(inbuf, fd)) {
		char *p, *comment;
		char *inp, *outp;

		p = strchr(STRBUF(inbuf), '\n'); if (p) *p = '\0';
		/* Strip whitespace */
		for (inp=outp=STRBUF(inbuf); ((*inp >= ' ') && (*inp != '#')); inp++) {
			if (isspace((int)*inp)) {
			}
			else {
				*outp = *inp;
				outp++;
			}
		}
		*outp = '\0';
		if (strlen(inp)) memmove(outp, inp, strlen(inp)+1);
		strbufferrecalc(inbuf);

		if (STRBUFLEN(inbuf) && (*STRBUF(inbuf) != '#') && (p = strchr(STRBUF(inbuf), '=')) ) {
			testspec_t *newtest;
			char *hname, *tname;

			hname = gethname(STRBUF(inbuf));
			tname = gettname(STRBUF(inbuf));

			if (hname && tname) {
				*p = '\0';
				comment = strchr(p+1, '#');
				if (comment) *comment = '\0';
				newtest = (testspec_t *) malloc(sizeof(testspec_t));
				newtest->reshostname = strdup(gethname(STRBUF(inbuf)));
				newtest->restestname = strdup(gettname(STRBUF(inbuf)));
				newtest->expression = strdup(p+1);
				newtest->comment = (comment ? strdup(comment+1) : NULL);
				newtest->resultexpr = NULL;
				newtest->valuelist = NULL;
				newtest->result = -1;
				newtest->errbuf = NULL;
				newtest->next = testhead;
				testhead = newtest;
				testcount++;
			}
			else {
				errprintf("Invalid combo test %s - missing host/test names. Perhaps you need to escape dashes?\n", STRBUF(inbuf));
			}
		}
	}

	fclose(fd);
	freestrbuffer(inbuf);
}

static int gethobbitdvalue(char *hostname, char *testname, char **errptr)
{
	static char *board = NULL;
	int hobbitdresult;
	int result = COL_CLEAR;
	char *pattern, *found, *colstr;

	if (board == NULL) {
		sendreturn_t *sres = newsendreturnbuf(1, NULL);

		hobbitdresult = sendmessage("hobbitdboard fields=hostname,testname,color", NULL, BBTALK_TIMEOUT, sres);
		board = getsendreturnstr(sres, 1);

		if ((hobbitdresult != BB_OK) || (board == NULL)) {
			board = "";
			*errptr += sprintf(*errptr, "Could not access hobbitd board, error %d\n", hobbitdresult);
			return COL_CLEAR;
		}

		freesendreturnbuf(sres);
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
		/* hostname|testname|color */
		colstr = found + strlen(pattern);
		result = parse_color(colstr);
	}

	xfree(pattern);
	return result;
}

static long getvalue(char *hostname, char *testname, int *color, char **errbuf)
{
	testspec_t *walk;
	char errtext[1024];
	char *errptr;

	*color = -1;
	errptr = errtext; 
	*errptr = '\0';

	/* First check if it is one of our own tests */
	for (walk = testhead; (walk && ( (strcmp(walk->reshostname, hostname) != 0) || (strcmp(walk->restestname, testname) != 0) ) ); walk = walk->next);
	if (walk != NULL) {
		/* It is a combo test they want the result of. */
		return walk->result;
	}

	*color = gethobbitdvalue(hostname, testname, &errptr);

	/* Save error messages */
	if (strlen(errtext) > 0) {
		if (*errbuf == NULL)
			*errbuf = strdup(errtext);
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
				char *hname, *tname;

				*symp = '\0';
				insymbol = 0;
				hname = gethname(symbol); 
				tname = gettname(symbol);
				if (hname && tname) {
					oneval = getvalue(gethname(symbol), gettname(symbol), &onecolor, errbuf);
				}
				else {
					errprintf("Invalid data for symbol calculation - missing host/testname: %s\n",
						  symbol);
					oneval = 0;
					onecolor = COL_CLEAR;
				}

				sprintf(outp, "%ld", oneval);
				outp += strlen(outp);

				newval = (value_t *) malloc(sizeof(value_t));
				newval->symbol = strdup(symbol);
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

	if (resultexpr) *resultexpr = strdup(expr);
	dbgprintf("Symbolic '%s' converted to '%s'\n", symbolicexpr, expr);

	error = 0; 
	result = compute(expr, &error);

	if (error) {
		sprintf(errtext, "compute(%s) returned error %d\n", expr, error);
		if (*errbuf == NULL) {
			*errbuf = strdup(errtext);
		}
		else {
			*errbuf = (char *)realloc(*errbuf, strlen(*errbuf)+strlen(errtext)+1);
			strcat(*errbuf, errtext);
		}
	}

	*valuelist = valhead;
	return result;
}

static char *printify(char *exp, int cleanexpr)
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

int update_combotests(int showeval, int cleanexpr)
{
	testspec_t *t;
	int pending;
	int remaining = 0;

	init_timestamp();
	loadtests();

	/*
	 * Loop over the tests to allow us "forward refs" in expressions.
	 * We continue for as long as progress is being made.
	 */
	remaining = testcount;
	do {
		pending = remaining;
		for (t=testhead; (t); t = t->next) {
			if (t->result == -1) {
				t->result = evaluate(t->expression, &t->resultexpr, &t->valuelist, &t->errbuf);
				if (t->result != -1) remaining--;
			}
		}

	} while (pending != remaining);

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
			addtostatus(printify(t->expression, cleanexpr));
			addtostatus(" = ");
			addtostatus(printify(t->resultexpr, cleanexpr));
			addtostatus(" = ");
			sprintf(msgline, "%ld\n", t->result);
			addtostatus(msgline);

			for (vwalk = t->valuelist; (vwalk); vwalk = vwalk->next) {
				sprintf(msgline, "&%s <a href=\"%s/bb-hostsvc.sh?HOST=%s&amp;SERVICE=%s\">%s</a>\n",
					colorname(vwalk->color), xgetenv("CGIBINURL"), gethname(vwalk->symbol), gettname(vwalk->symbol), vwalk->symbol);
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

int main(int argc, char *argv[])
{
	int argi; 
	int showeval = 1;
	int cleanexpr = 0;

	setup_signalhandler(argv[0]);

	for (argi = 1; (argi < argc); argi++) {
		if ((strcmp(argv[argi], "--help") == 0)) {
			printf("%s version %s\n\n", argv[0], VERSION);
			printf("Usage:\n%s [--quiet] [--clean] [--debug] [--no-update]\n", argv[0]);
			exit(0);
		}
		else if ((strcmp(argv[argi], "--version") == 0)) {
			printf("%s version %s\n", argv[0], VERSION);
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
	}

	return update_combotests(showeval, cleanexpr);
}

