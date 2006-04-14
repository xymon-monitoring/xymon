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

static char rcsid[] = "$Id: logfetch.c,v 1.11 2006-04-14 16:08:34 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <regex.h>
#include <pwd.h>
#include <grp.h>

#include "libbbgen.h"

/* Is it ok for these to be hardcoded ? */
#define MAXMINUTES 30
#define POSCOUNT ((MAXMINUTES / 5) + 1)
#define LINES_AFTER_TRIGGER 10

typedef enum { C_NONE, C_LOG, C_FILE } checktype_t;

typedef struct logdef_t {
	off_t lastpos[POSCOUNT];
	char *trigger;
	char *ignore;
	int maxbytes;
} logdef_t;

typedef struct filedef_t {
	int domd5, dosha1, dormd160;
} filedef_t;

typedef struct checkdef_t {
	char *filename;
	checktype_t checktype;
	struct checkdef_t *next;
	union {
		logdef_t logcheck;
		filedef_t filecheck;
	} check;
} checkdef_t;

checkdef_t *checklist = NULL;

char *logdata(char *filename, logdef_t *logdef, int *truncated)
{
	static char *result = NULL;
	char *buf = NULL;
	char *startpos;
	FILE *fd;
	struct stat st;
	off_t bufsz, n;
	int i;

	*truncated = 0;

	fd = fopen(filename, "r");
	if (fd == NULL) {
		result = (char *)malloc(1024 + strlen(filename));
		sprintf(result, "Cannot open logfile %s : %s\n", filename, strerror(errno));
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
		sprintf("Error while reading logfile %s : %s\n", filename, strerror(errno));
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
					n -= (eofs - bofs);
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
	 */
	if ((n > logdef->maxbytes) && logdef->trigger) {
		/*
		 * Check if there's a trigger string anywhere in the data - 
		 * if there is, then we'll skip to that trigger string.
		 */
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

			/* If it's still too big, show the 10 lines after the trigger, and
			 * then skip until it will fit.
			 */
			if (n > logdef->maxbytes) {
				char *eoln;
				int count = 0;

				eoln = startpos;
				while (eoln && (count < LINES_AFTER_TRIGGER)) {
					eoln = strchr(eoln, '\n');
					if (eoln) eoln++;
					count++;
				}

				if (eoln) {
					int used, left, keep, togo;
					
					left = strlen(eoln);
					if (left > 20) {
						memcpy(eoln, "...<TRUNCATED>...\n", 18);
						eoln = strchr(eoln, '\n'); eoln++;
						used = (eoln - startpos);
						keep = (logdef->maxbytes - used);
						togo = (left - keep);
						memmove(eoln, eoln+togo, keep+1);
						n = n - togo + 18;
					}
				}
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

char *ftypestr(unsigned int mode, char *symlink)
{
	static char *result = NULL;
	char *s = "unknown";

	if (S_ISREG(mode)) s = "file";
	if (S_ISDIR(mode)) s = "directory";
	if (S_ISCHR(mode)) s = "char-device";
	if (S_ISBLK(mode)) s = "block-device";
	if (S_ISFIFO(mode)) s = "FIFO";
	if (S_ISSOCK(mode)) s = "socket";

	if (symlink == NULL) return s;

	/* Special handling for symlinks */
	if (result) free(result);

	result = (char *)malloc(strlen(s) + strlen(symlink) + 100);
	sprintf(result, "%s, symlink -> %s", s, symlink);
	return result;
}

char *fmodestr(unsigned int mode)
{
	static char modestr[11];

	if (S_ISREG(mode)) modestr[0] = '-';
	else if (S_ISDIR(mode)) modestr[0] = 'd';
	else if (S_ISCHR(mode)) modestr[0] = 'c';
	else if (S_ISBLK(mode)) modestr[0] = 'b';
	else if (S_ISFIFO(mode)) modestr[0] = 'p';
	else if (S_ISLNK(mode)) modestr[0] = 'l';
	else if (S_ISSOCK(mode)) modestr[0] = 's';
	else modestr[0] = '?';

	modestr[1] = ((mode & S_IRUSR) ? 'r' : '-');
	modestr[2] = ((mode & S_IWUSR) ? 'w' : '-');
	modestr[3] = ((mode & S_IXUSR) ? 'x' : '-');
	modestr[4] = ((mode & S_IRGRP) ? 'r' : '-');
	modestr[5] = ((mode & S_IWGRP) ? 'w' : '-');
	modestr[6] = ((mode & S_IXGRP) ? 'x' : '-');
	modestr[7] = ((mode & S_IROTH) ? 'r' : '-');
	modestr[8] = ((mode & S_IWOTH) ? 'w' : '-');
	modestr[9] = ((mode & S_IXOTH) ? 'x' : '-');

	if ((mode & S_ISUID)) modestr[3] = 's';
	if ((mode & S_ISGID)) modestr[6] = 's';

	modestr[10] = '\0';

	return modestr;
}

char *timestr(time_t tstamp)
{
	static char result[20];

	strftime(result, sizeof(result), "%Y/%m/%d-%H:%M:%S", localtime(&tstamp));
	return result;
}

char *filesum(char *fn, char *dtype)
{
	static char *result = NULL;
	digestctx_t *ctx;
	FILE *fd;
	unsigned char buf[8192];
	int buflen;

        if ((ctx = digest_init(dtype)) == NULL) return "";

	fd = fopen(fn, "r"); if (fd == NULL) return "";
	while ((buflen = fread(buf, 1, sizeof(buf), fd)) > 0) digest_data(ctx, buf, buflen);
	fclose(fd);

	if (result) xfree(result);
	result = strdup(digest_done(ctx));

	return result;
}

void printfiledata(FILE *fd, char *fn, int domd5, int dosha1, int dormd160)
{
	struct stat st;
	struct passwd *pw;
	struct group *gr;
	int staterror;
	char linknam[PATH_MAX];
	time_t now = time(NULL);

	*linknam = '\0';
	staterror = lstat(fn, &st);
	if ((staterror == 0) && S_ISLNK(st.st_mode)) {
		if (readlink(fn, linknam, sizeof(linknam)) == -1) *linknam = '\0';
		staterror = stat(fn, &st);
	}

	if (staterror == -1) {
		fprintf(fd, "ERROR: %s\n", strerror(errno));
	}
	else {
		pw = getpwuid(st.st_uid);
		gr = getgrgid(st.st_gid);

		fprintf(fd, "type:%o (%s)\n", 
			(st.st_mode & S_IFMT), 
			ftypestr(st.st_mode, (*linknam ? linknam : NULL)));
		fprintf(fd, "mode:%o (%s)\n", 
			(st.st_mode & (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)), 
			fmodestr(st.st_mode));
		fprintf(fd, "linkcount:%d\n", st.st_nlink);
		fprintf(fd, "owner:%u (%s)\n", st.st_uid, (pw ? pw->pw_name : ""));
		fprintf(fd, "group:%u (%s)\n", st.st_gid, (gr ? gr->gr_name : ""));
		fprintf(fd, "size:%lu\n", (unsigned long) st.st_size);
		fprintf(fd, "clock:%u (%s)\n", (unsigned int)now, timestr(now));
		fprintf(fd, "atime:%u (%s)\n", (unsigned int)st.st_atime, timestr(st.st_atime));
		fprintf(fd, "ctime:%u (%s)\n", (unsigned int)st.st_ctime, timestr(st.st_ctime));
		fprintf(fd, "mtime:%u (%s)\n", (unsigned int)st.st_mtime, timestr(st.st_mtime));
		if (S_ISREG(st.st_mode)) {
			if      (domd5) fprintf(fd, "%s\n", filesum(fn, "md5"));
			else if (dosha1) fprintf(fd, "%s\n", filesum(fn, "sha1"));
			else if (dormd160) fprintf(fd, "%s\n", filesum(fn, "rmd160"));
		}
	}

	fprintf(fd, "\n");
}

int loadconfig(char *cfgfn)
{
	FILE *fd;
	char l[PATH_MAX + 1024];
	checkdef_t *currcfg = NULL;
	checkdef_t *firstpipeitem = NULL;

	/* Config items are in the form:
	 *    log:filename:maxbytes
	 *    ignore ignore-regexp (optional)
	 *    trigger trigger-regexp (optional)
	 *
	 *    file:filename
	 */
	fd = fopen(cfgfn, "r"); if (fd == NULL) return 1;
	while (fgets(l, sizeof(l), fd) != NULL) {
		char *p, *filename;
		int maxbytes, domd5, dosha1, dormd160;

		p = strchr(l, '\n'); if (p) *p = '\0';
		p = l + strspn(l, " \t");
		if ((*p == '\0') || (*p == '#')) continue;

		if ((strncmp(l, "log:", 4) == 0) || (strncmp(l, "file:", 4) == 0)) {
			checktype_t checktype;
			char *tok;

			filename = NULL; maxbytes = -1; domd5 = dosha1 = dormd160 = 0;
			tok = strtok(l, ":");

			if (strcmp(tok, "log") == 0) checktype = C_LOG;
			else if (strcmp(tok, "file") == 0) checktype = C_FILE;
			else checktype = C_NONE;

			filename = strtok(NULL, ":"); if (filename) tok = strtok(NULL, ":");
			switch (checktype) {
			  case C_LOG:
				if (tok) maxbytes = atoi(tok);
				break;

			  case C_FILE:
				maxbytes = 0; /* Needed to get us into the put-into-list code */
				if (tok) {
					if (strcmp(tok, "md5") == 0) domd5 = 1;
					else if (strcmp(tok, "sha1") == 0) dosha1 = 1;
					else if (strcmp(tok, "rmd160") == 0) dormd160 = 1;
				}
				break;

			  case C_NONE:
				break;
			}

			if ((filename != NULL) && (maxbytes != -1)) {
				checkdef_t *newitem;

				firstpipeitem = NULL;

				if (*filename == '`') {
					/* Run the command to get filenames */
					char *cmd;
					FILE *fd;

					cmd = filename+1;
					p = strchr(cmd, '`'); if (p) *p = '\0';
					fd = popen(cmd, "r");
					if (fd) {
						char pline[PATH_MAX+1];
						char *p;

						while (fgets(pline, sizeof(pline), fd)) {
							p = pline + strcspn(pline, "\r\n"); *p = '\0';

							newitem = calloc(sizeof(checkdef_t), 1);

							newitem->checktype = checktype;
							newitem->filename = strdup(pline);

							switch (checktype) {
					  		  case C_LOG:
								newitem->check.logcheck.maxbytes = maxbytes;
								break;
							  case C_FILE:
								newitem->check.filecheck.domd5 = domd5;
								newitem->check.filecheck.dosha1 = dosha1;
								newitem->check.filecheck.dormd160 = dormd160;
								break;
					  		  case C_NONE:
								break;
							}

							newitem->next = checklist;
							checklist = newitem;

							/*
							 * Since we insert new items at the head of the list,
							 * currcfg points to the first item in the list of
							 * these log configs. firstpipeitem points to the
							 * last item inside the list which is part of this
							 * configuration.
							 */
							currcfg = newitem;
							if (!firstpipeitem) firstpipeitem = newitem;
						}

						pclose(fd);
					}
				}
				else {
					newitem = calloc(sizeof(checkdef_t), 1);
					newitem->filename = strdup(filename);
					newitem->checktype = checktype;

					switch (checktype) {
					  case C_LOG:
						newitem->check.logcheck.maxbytes = maxbytes;
						break;
					  case C_FILE:
						newitem->check.filecheck.domd5 = domd5;
						newitem->check.filecheck.dosha1 = dosha1;
						newitem->check.filecheck.dormd160 = dormd160;
						break;
					  case C_NONE:
						break;
					}

					newitem->next = checklist;
					checklist = newitem;

					currcfg = newitem;
				}
			}
			else {
				currcfg = NULL;
				firstpipeitem = NULL;
			}
		}
		else if (currcfg && (currcfg->checktype == C_LOG)) {
			if (strncmp(l, "ignore ", 7) == 0) {
				p = l + 7; p += strspn(p, " \t");

				if (firstpipeitem) {
					/* Fill in this ignore expression on all items in this pipe set */
					checkdef_t *walk = currcfg;

					do {
						walk->check.logcheck.ignore = strdup(p);
						walk = walk->next;
					} while (walk && (walk != firstpipeitem->next));
				}
				else {
					currcfg->check.logcheck.ignore = strdup(p);
				}
			}
			else if (strncmp(l, "trigger ", 8) == 0) {
				p = l + 8; p += strspn(p, " \t");

				if (firstpipeitem) {
					/* Fill in this trigger expression on all items in this pipe set */
					checkdef_t *walk = currcfg;

					do {
						walk->check.logcheck.trigger = strdup(p);
						walk = walk->next;
					} while (walk && (walk != firstpipeitem->next));
				}
				else {
					currcfg->check.logcheck.trigger = strdup(p);
				}
			}
		}
		else if (currcfg && (currcfg->checktype == C_FILE)) {
			/* Nothing */
		}
		else if (currcfg && (currcfg->checktype == C_NONE)) {
			/* Nothing */
		}
		else {
			currcfg = NULL;
			firstpipeitem = NULL;
		}
	}

	fclose(fd);
	return 0;
}

void loadlogstatus(char *statfn)
{
	FILE *fd;
	char l[PATH_MAX + 1024];

	fd = fopen(statfn, "r");
	if (!fd) return;

	while (fgets(l, sizeof(l), fd)) {
		char *fn, *tok;
		checkdef_t *walk;
		int i;

		tok = strtok(l, ":"); if (!tok) continue;
		fn = tok;
		for (walk = checklist; (walk && ((walk->checktype != C_LOG) || (strcmp(walk->filename, fn) != 0))); walk = walk->next) ;
		if (!walk) continue;

		for (i=0; (tok && (i < POSCOUNT)); i++) {
			tok = strtok(NULL, ":\n");
			if (tok) walk->check.logcheck.lastpos[i] = atol(tok);
		}
	}

	fclose(fd);
}

void savelogstatus(char *statfn)
{
	FILE *fd;
	checkdef_t *walk;

	fd = fopen(statfn, "w");
	if (fd == NULL) return;

	for (walk = checklist; (walk); walk = walk->next) {
		int i;

		if (walk->checktype != C_LOG) continue;

		fprintf(fd, "%s", walk->filename);
		for (i = 0; (i < POSCOUNT); i++) fprintf(fd, ":%lu", walk->check.logcheck.lastpos[i]);
		fprintf(fd, "\n");
	}
	fclose(fd);
}

int main(int argc, char *argv[])
{
	char *cfgfn = argv[1];
	char *statfn = argv[2];
	checkdef_t *walk;

	if ((cfgfn == NULL) || (statfn == NULL)) return 1;

	if (loadconfig(cfgfn) != 0) return 1;
	loadlogstatus(statfn);

	for (walk = checklist; (walk); walk = walk->next) {
		char *data;
		int truncflag;
		checkdef_t *fwalk;

		switch (walk->checktype) {
		  case C_LOG:
			data = logdata(walk->filename, &walk->check.logcheck, &truncflag);
			fprintf(stdout, "[msgs:%s]\n", walk->filename);
			if (truncflag) fprintf(stdout, "<truncated>\n");
			fprintf(stdout, "%s\n", data);
			free(data);

			/* See if there's a special "file:" entry for this logfile */
			for (fwalk = checklist; (fwalk && ((fwalk->checktype != C_FILE) || (strcmp(fwalk->filename, walk->filename) != 0))); fwalk = fwalk->next) ;
			if (fwalk == NULL) {
				/* No specific file: entry, so make sure the logfile metadata is available */
				fprintf(stdout, "[file:%s]\n", walk->filename);
				printfiledata(stdout, walk->filename, 0, 0, 0);
			}
			break;

		  case C_FILE:
			fprintf(stdout, "[file:%s]\n", walk->filename);
			printfiledata(stdout, walk->filename, 
					walk->check.filecheck.domd5, 
					walk->check.filecheck.dosha1,
					walk->check.filecheck.dormd160);
			break;

		  case C_NONE:
			break;
		}
	}

	savelogstatus(statfn);

	return 0;
}

