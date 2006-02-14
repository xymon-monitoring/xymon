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

static char rcsid[] = "$Id: logfetch.c,v 1.5 2006-02-14 13:55:02 henrik Exp $";

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
	char *ignore;
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

	/* Strip out the ignored lines */
	if (logdef->ignore) {
		regex_t expr;
		int status;

		status = regcomp(&expr, logdef->ignore, REG_EXTENDED|REG_ICASE|REG_NOSUB);
		if (status == 0) {
			int bofs, eofs;

			bofs=0;
			while (*(buf + bofs)) {
				char savechar;

				eofs = bofs + strcspn(buf + bofs, "\n");
				savechar = *(buf + eofs);
				*(buf + eofs) = '\0';

				status = regexec(&expr, (buf + bofs), 0, NULL, 0);
				if (status == 0) {
					/* Ignore this line */
					memmove(buf+bofs, buf+eofs+1, (n-eofs+1));
					n -+ (eofs - bofs);
				}
				else {
					*(buf + eofs) = savechar;
					bofs = eofs+1;
				}
			}

			regfree(&expr);
		}
	}

	/*
	 * If it's too big, we may need to truncate ie. 
	 * First check if there's a trigger string anywhere in the data - 
	 * if there is, then we'll skip to that trigger string.
	 */
	if ((n > logdef->maxbytes) && logdef->trigger) {
		if (logdef->trigger) {
			regex_t expr;
			regmatch_t pmatch[1];
			int status;

			status = regcomp(&expr, logdef->trigger, REG_EXTENDED|REG_ICASE);
			if (status == 0) {
				status = regexec(&expr, buf, 1, pmatch, 0);
				if (status == 0) {
					startpos += pmatch[0].rm_so;
					n -= pmatch[0].rm_so;
				}
				regfree(&expr);
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

	/* Avoid sending a '[' as the first char on a line */
	{
		char *p;

		p = result;
		while (p) {
			if (*p == '[') *p = '.';
			p = strstr(p, "\n[");
			if (p) p++;
		}
	}

	return result;
}

int loadconfig(char *cfgfn)
{
	FILE *fd;
	char l[PATH_MAX + 1024];
	logdef_t *currcfg = NULL;

	/* Config items are in the form:
	 *    log:filename:maxbytes
	 *    ignore ignore-regexp (optional)
	 *    trigger trigger-regexp (optional)
	 */
	fd = fopen(cfgfn, "r"); if (fd == NULL) return 1;
	while (fgets(l, sizeof(l), fd) != NULL) {
		char *p, *filename;
		int maxbytes;

		p = strchr(l, '\n'); if (p) *p = '\0';
		p = l + strspn(l, " \t");
		if ((*p == '\0') || (*p == '#')) continue;

		if (strncmp(l, "log:", 4) == 0) {
			char *tok;

			filename = NULL; maxbytes = -1;
			tok = strtok(l, ":");
			if (tok) filename = strtok(NULL, ":");
			if (filename) tok = strtok(NULL, ":");
			if (tok) maxbytes = atoi(tok);

			if ((filename != NULL) && (maxbytes != -1)) {
				logdef_t *newitem = calloc(sizeof(logdef_t), 1);
				newitem->filename = strdup(filename);
				newitem->maxbytes = maxbytes;
				newitem->next = loglist;
				loglist = newitem;

				currcfg = newitem;
			}
			else {
				currcfg = NULL;
			}
		}
		else if (currcfg && (strncmp(l, "ignore ", 7) == 0)) {
			p = l + 7; p += strspn(p, " \t");
			currcfg->ignore = strdup(p);
		}
		else if (currcfg && (strncmp(l, "trigger ", 8) == 0)) {
			p = l + 8; p += strspn(p, " \t");
			currcfg->trigger = strdup(p);
		}
		else currcfg = NULL;
	}

	fclose(fd);
	return 0;
}

void loadstatus(char *statfn)
{
	FILE *fd;
	char l[PATH_MAX + 1024];

	fd = fopen(statfn, "r");
	if (!fd) return;

	while (fgets(l, sizeof(l), fd)) {
		char *fn, *tok;
		logdef_t *lwalk;
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
}

void savestatus(char *statfn)
{
	FILE *fd;
	logdef_t *lwalk;

	fd = fopen(statfn, "w");
	if (fd == NULL) return;

	for (lwalk = loglist; (lwalk); lwalk = lwalk->next) {
		int i;

		fprintf(fd, "%s", lwalk->filename);
		for (i = 0; (i < POSCOUNT); i++) fprintf(fd, ":%lu", lwalk->lastpos[i]);
		fprintf(fd, "\n");
	}
	fclose(fd);
}

int main(int argc, char *argv[])
{
	char *cfgfn = argv[1];
	char *statfn = argv[2];
	logdef_t *lwalk;

	if ((cfgfn == NULL) || (statfn == NULL)) return 1;

	if (loadconfig(cfgfn) != 0) return 1;
	loadstatus(statfn);

	for (lwalk = loglist; (lwalk); lwalk = lwalk->next) {
		char *data;
		int truncflag;

		data = logdata(lwalk, &truncflag);
		fprintf(stdout, "[msgs:%s]\n", lwalk->filename);
		if (truncflag) fprintf(stdout, "<truncated>\n");
		fprintf(stdout, "%s\n", data);
		free(data);
	}

	savestatus(statfn);

	return 0;
}

