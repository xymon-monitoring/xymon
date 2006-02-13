/*----------------------------------------------------------------------------*/
/* Hobbit client logfile collection tool.                                     */
/* This tool retrieves data from logfiles. If run continuously, it will pick  */
/* out the data stored in the logfile over the past 6 runs (30 minutes with   */
/* the default Hobbit client polling frequency) and send these data to stdout */
/* for inclusion in the hobbit "client" message.                              */
/*                                                                            */
/* Copyright (C) 2006 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <regex.h>

/* Is it ok for these to be hardcoded ? */
#define MAXMINUTES 30
#define POSCOUNT ((MAXMINUTES / 5) + 1)

typedef struct logdef_t {
	char *filename;
	off_t lastpos[POSCOUNT];
	char *trigger;
	int maxbytes;
	struct logdef_t *next;
} logdef_t;

logdef_t *loglist = NULL;

char *logdata(logdef_t *logdef, int *truncated)
{
	static char *result = NULL;
	char *buf = NULL;
	char *startpos;
	FILE *fd;
	struct stat st;
	off_t bufsz, n;
	int i;

	*truncated = 0;

	fd = fopen(logdef->filename, "r");
	if (fd == NULL) {
		result = (char *)malloc(1024 + strlen(logdef->filename));
		sprintf(result, "Cannot open logfile %s : %s\n", logdef->filename, strerror(errno));
		return result;
	}

	/*
	 * See how large the file is, and decide where to start reading.
	 * Save the last POSCOUNT positions so we can scrap 5 minutes of data
	 * from one run to the next.
	 */
	fstat(fileno(fd), &st);
	if (st.st_size < logdef->lastpos[0]) {
		/* Logfile shrank - probably it was rotated */
		fseek(fd, 0, SEEK_SET);
		for (i=0; (i < 7); i++) logdef->lastpos[i] = 0;
	}
	else {
		fseek(fd, logdef->lastpos[6], SEEK_SET);
		for (i=6; (i > 0); i--) logdef->lastpos[i] = logdef->lastpos[i-1];
		logdef->lastpos[0] = st.st_size;
	}

	bufsz = st.st_size - ftell(fd);
	if (bufsz < 1024) bufsz = 1024;
	startpos = buf = (char *)malloc(bufsz + 1);
	n = fread(buf, 1, bufsz, fd);

	if (n >= 0) {
		*(buf + n) = '\0';
	}
	else {
		sprintf("Error while reading logfile %s : %s\n", logdef->filename, strerror(errno));
	}

	fclose(fd);

	/*
	 * If it's too big, we may need to truncate ie. 
	 * First check if there's a trigger string anywhere in the data - 
	 * if there is, then we'll skip to that trigger string.
	 */
	if ((n > logdef->maxbytes) && logdef->trigger) {
		if (logdef->trigger) {
			regex_t *expr;
			regmatch_t pmatch[1];
			int status;

			status = regcomp(expr, logdef->trigger, REG_EXTENDED|REG_ICASE);
			if (status == 0) {
				status = regexec(expr, buf, 1, pmatch, 0);
				if (status == 0) {
					startpos += pmatch[0].rm_so;
					n -= pmatch[0].rm_so;
				}
				regfree(expr);
			}
		}
	}

	/* If it's still too big, just drop what is too much */
	if (n > logdef->maxbytes) {
		startpos += (n - logdef->maxbytes);
		n = logdef->maxbytes;
	}

	if (startpos != buf) {
		result = strdup(startpos);
		free(buf);
		*truncated = 1;
	}
	else {
		result = buf;
	}

	return result;
}

int main(int argc, char *argv[])
{
	char *cfgfn = argv[1];
	char *statfn = argv[2];
	FILE *fd;
	char l[PATH_MAX + 1024];
	logdef_t *lwalk;

	if ((cfgfn == NULL) || (statfn == NULL)) return 1;

	fd = fopen(cfgfn, "r"); if (fd == NULL) return 1;
	while (fgets(l, sizeof(l), fd)) {
		char *p, *tok;

		p = strchr(l, '\n'); if (p) *p = '\0';

		logdef_t *newitem = calloc(sizeof(logdef_t), 1);
		tok = l; p = strchr(tok, ':');
		if ((*tok == '\0') || (*tok == '#')) continue;
		if (p) { *p = '\0'; newitem->filename = strdup(tok); tok = p+1; p = strchr(tok, ':'); }
		if (p) { *p = '\0'; newitem->maxbytes = atoi(tok); tok = p+1; }
		if (*tok) newitem->trigger = strdup(tok);
		newitem->next = loglist;
		loglist = newitem;
	}
	fclose(fd);

	fd = fopen(statfn, "r");
	while (fgets(l, sizeof(l), fd)) {
		char *fn, *tok;
		int i;

		tok = strtok(l, ":");
		if (tok) fn = tok;
		for (lwalk = loglist; (lwalk && strcmp(lwalk->filename, fn)); lwalk = lwalk->next) ;
		if (!lwalk) continue;

		for (i=0; (tok && (i < POSCOUNT)); i++) {
			tok = strtok(NULL, ":\n");
			if (tok) lwalk->lastpos[i] = atol(tok);
		}
	}
	fclose(fd);

	for (lwalk = loglist; (lwalk); lwalk = lwalk->next) {
		char *data;
		int truncflag;
		
		data = logdata(lwalk, &truncflag);
		fprintf(stdout, "[msgs:%s]\n", lwalk->filename);
		if (truncflag) fprintf(stdout, "<truncated>\n");
		fprintf(stdout, "%s\n", data);
	}

	fd = fopen(statfn, "w");
	for (lwalk = loglist; (lwalk); lwalk = lwalk->next) {
		int i;

		fprintf(fd, "%s", lwalk->filename);
		for (i = 0; (i < POSCOUNT); i++) fprintf(fd, ":%lu", lwalk->lastpos[i]);
		fprintf(fd, "\n");
	}
	fclose(fd);

	return 0;
}

