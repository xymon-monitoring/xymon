/*----------------------------------------------------------------------------*/
/* Xymon client logfile collection tool.                                      */
/* This tool retrieves data from logfiles. If run continuously, it will pick  */
/* out the data stored in the logfile over the past 6 runs (30 minutes with   */
/* the default Xymon client polling frequency) and send these data to stdout  */
/* for inclusion in the Xymon "client" message.                               */
/*                                                                            */
/* Copyright (C) 2006-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
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

/* Some systems do not have the S_ISSOCK macro for stat() */
#ifdef SCO_SV
#include <cpio.h>
#define S_ISSOCK(m)   (((m) & S_IFMT) == C_ISSOCK)
#endif

#include "libxymon.h"

/* Is it ok for these to be hardcoded ? */
#define MAXCHECK   102400   /* When starting, dont look at more than 100 KB of data */
#define MAXMINUTES 30
#define POSCOUNT ((MAXMINUTES / 5) + 1)
#define LINES_AROUND_TRIGGER 5

typedef enum { C_NONE, C_LOG, C_FILE, C_DIR, C_COUNT } checktype_t;

typedef struct logdef_t {
#ifdef _LARGEFILE_SOURCE
	off_t lastpos[POSCOUNT];
	off_t maxbytes;
#else
	long lastpos[POSCOUNT];
	long maxbytes;
#endif
	char **trigger;
	int triggercount;
	char **ignore;
	int ignorecount;
} logdef_t;

typedef struct filedef_t {
	int domd5, dosha1, dormd160;
} filedef_t;

typedef struct countdef_t {
	int patterncount;
	char **patternnames;
	char **patterns;
	int *counts;
} countdef_t;

typedef struct checkdef_t {
	char *filename;
	checktype_t checktype;
	struct checkdef_t *next;
	union {
		logdef_t logcheck;
		filedef_t filecheck;
		countdef_t countcheck;
	} check;
} checkdef_t;

checkdef_t *checklist = NULL;


FILE *fileopen(char *filename, int *err)
{
	/* Open a file */
	FILE *fd;

#ifdef BIG_SECURITY_HOLE
	get_root();
#endif

	fd = fopen(filename, "r");
	if (err) *err = errno;

#ifdef BIG_SECURITY_HOLE
	drop_root();
#endif

	return fd;
}

/*
 * A wrapper for fgets() which eats embedded 0x00 characters in the stream.
 */
char *fgets_nonull(char *buf, size_t size, FILE *stream) {
	char *in, *out, *end;

	if (fgets(buf, size - 1, stream) == NULL) 
	        return NULL;

	end = memchr(buf, '\n', size - 1); 

	if (end == NULL) 
	        end = buf + (size - 1); 
	else 
	        end++;

	for (in = out = buf; in < end; in++) {
	        if (*in != '\0') 
		       *out++ = *in;
	}

	*out = '\0';

	return buf;
}


char *logdata(char *filename, logdef_t *logdef)
{
	static char *buf, *replacement = NULL;
	char *startpos, *fillpos, *triggerstartpos, *triggerendpos;
	FILE *fd;
	struct stat st;
	size_t bytesread, bytesleft;
	int openerr, i, status, triggerlinecount, done;
	char *linepos[2*LINES_AROUND_TRIGGER+1];
	int lpidx;
	regex_t *ignexpr = NULL, *trigexpr = NULL;
#ifdef _LARGEFILE_SOURCE
	off_t bufsz;
#else
	long bufsz;
#endif

	char *(*triggerptrs)[2] = NULL;
	unsigned int triggerptrs_count = 0;

	if (buf) free(buf);
	buf = NULL;

	if (replacement) free(replacement);
	replacement = NULL;

	fd = fileopen(filename, &openerr);
	if (fd == NULL) {
		buf = (char *)malloc(1024 + strlen(filename));
		sprintf(buf, "Cannot open logfile %s : %s\n", filename, strerror(openerr));
		return buf;
	}

	/*
	 * See how large the file is, and decide where to start reading.
	 * Save the last POSCOUNT positions so we can scrap 5 minutes of data
	 * from one run to the next.
	 */
	fstat(fileno(fd), &st);
	if ((st.st_size < logdef->lastpos[0]) || (st.st_size < logdef->lastpos[POSCOUNT-1])) {
		/*
		 * Logfile shrank - probably it was rotated.
		 * Start from beginning of file.
		 */
		errprintf("logfetch: File %s shrank from >=%zu to %zu bytes in size. Probably rotated; clearing position state\n", filename, logdef->lastpos[POSCOUNT-1], st.st_size);
		for (i=0; (i < POSCOUNT); i++) logdef->lastpos[i] = 0;
	}

	/* Go to the position we were at POSCOUNT-1 times ago (corresponds to 30 minutes) */
#ifdef _LARGEFILE_SOURCE
	fseeko(fd, logdef->lastpos[POSCOUNT-1], SEEK_SET);
	bufsz = st.st_size - ftello(fd);
	if (bufsz > MAXCHECK) {
		/*
		 * Too much data for us. We have to skip some of the old data.
		 */
		errprintf("logfetch: %s delta %zu bytes exceeds max buffer size %zu; skipping some data\n", filename, bufsz, MAXCHECK);
		logdef->lastpos[POSCOUNT-1] = st.st_size - MAXCHECK;
		fseeko(fd, logdef->lastpos[POSCOUNT-1], SEEK_SET);
		bufsz = st.st_size - ftello(fd);
	}
#else
	fseek(fd, logdef->lastpos[POSCOUNT-1], SEEK_SET);
	bufsz = st.st_size - ftell(fd);
	if (bufsz > MAXCHECK) {
		/*
		 * Too much data for us. We have to skip some of the old data.
		 */
		errprintf("logfetch: %s delta %zu bytes exceeds max buffer size %zu; skipping some data\n", filename, bufsz, MAXCHECK);
		logdef->lastpos[POSCOUNT-1] = st.st_size - MAXCHECK;
		fseek(fd, logdef->lastpos[POSCOUNT-1], SEEK_SET);
		bufsz = st.st_size - ftell(fd);
	}
#endif

	/* Shift position markers one down for the next round */
	for (i=POSCOUNT-1; (i > 0); i--) logdef->lastpos[i] = logdef->lastpos[i-1];
	logdef->lastpos[0] = st.st_size;

	/*
	 * Get our read buffer.
	 *
	 * NB: fgets() need some extra room in the input buffer.
	 *     If it is missing, we will never detect end-of-file 
	 *     because fgets() will read 0 bytes, but having read that
	 *     it still hasnt reached end-of-file status.
	 *     At least, on some platforms (Solaris, FreeBSD).
	 */
	bufsz += 1023;
	startpos = buf = (char *)malloc(bufsz + 1);
	if (buf == NULL) {
		/* Couldnt allocate the buffer */
		return "Out of memory";
	}

	/* Compile the regex patterns */
	if (logdef->ignorecount) {
               int i, realcount = 0;
		ignexpr = (regex_t *) malloc(logdef->ignorecount * sizeof(regex_t));
		for (i=0; (i < logdef->ignorecount); i++) {
			dbgprintf(" - compiling IGNORE regex: %s\n", logdef->ignore[i]);
			status = regcomp(&ignexpr[realcount++], logdef->ignore[i], REG_EXTENDED|REG_ICASE|REG_NOSUB);
			if (status != 0) {
				char regbuf[1000];
				regerror(status, &ignexpr[--realcount], regbuf, sizeof(regbuf));	/* re-decrement realcount here */
				errprintf("logfetch: could not compile ignore regex '%s': %s\n", logdef->ignore[i], regbuf);
				logdef->ignore[i] = NULL;
			}
		}
		logdef->ignorecount = realcount;
	}
	if (logdef->triggercount) {
		int i, realcount = 0;
		trigexpr = (regex_t *) malloc(logdef->triggercount * sizeof(regex_t));
		for (i=0; (i < logdef->triggercount); i++) {
			dbgprintf(" - compiling TRIGGER regex: %s\n", logdef->trigger[i]);
			status = regcomp(&trigexpr[realcount++], logdef->trigger[i], REG_EXTENDED|REG_ICASE|REG_NOSUB);
			if (status != 0) {
				char regbuf[1000];
				regerror(status, &trigexpr[--realcount], regbuf, sizeof(regbuf));	/* re-decrement realcount here */
				errprintf("logfetch: could not compile trigger regex '%s': %s\n", logdef->trigger[i], regbuf);
				logdef->trigger[i] = NULL;
			}
		}
		logdef->triggercount = realcount;
	}
	triggerstartpos = triggerendpos = NULL;
	triggerlinecount = 0;
	memset(linepos, 0, sizeof(linepos)); lpidx = 0;

	/* 
	 * Read data.
	 * Discard the ignored lines as we go.
	 * Remember the last trigger line we see.
	 */
	fillpos = buf;
	bytesleft = bufsz;
	done = 0;
	while (!ferror(fd) && (bytesleft > 0) && !done && (fgets_nonull(fillpos, bytesleft, fd) != NULL)) {
		if (*fillpos == '\0') {
			/*
			 * fgets() can return an empty buffer without flagging
			 * end-of-file. It should not happen anymore now that
			 * we have extended the buffer to have room for the
			 * terminating \0 byte, but if it does then we will
			 * catch it here.
			 */
			dbgprintf(" - empty buffer returned; assuming eof\n");
			done = 1;
			continue;
		}

		/* Check ignore pattern */
		if (logdef->ignorecount) {
			int i, match = 0;

			for (i=0; ((i < logdef->ignorecount) && !match); i++) {
				match = (regexec(&ignexpr[i], fillpos, 0, NULL, 0) == 0);
				if (match) dbgprintf(" - line matched ignore %d: %s", i, fillpos); // fgets stores the newline in
			}

			if (match) continue;
		}

		linepos[lpidx] = fillpos;

		/* See if this is a trigger line */
		if (logdef->triggercount) {
			int i, match = 0;

			for (i=0; ((i < logdef->triggercount) && !match); i++) {
				match = (regexec(&trigexpr[i], fillpos, 0, NULL, 0) == 0);
				if (match) dbgprintf(" - line matched trigger %d: %s", i, fillpos); // fgets stores the newline in
			}

			if (match) {
				int sidx;
				
				sidx = lpidx - LINES_AROUND_TRIGGER; 
				if (sidx < 0) sidx += (2*LINES_AROUND_TRIGGER + 1);
				triggerstartpos = linepos[sidx]; if (!triggerstartpos) triggerstartpos = buf;
				triggerlinecount = LINES_AROUND_TRIGGER;

				if (triggerptrs == NULL || (triggerptrs_count > 0 && triggerptrs[triggerptrs_count - 1][1] != NULL)) {
					dbgprintf(" - %s trigger line encountered; preparing trigger START & END positioning store\n", ((triggerptrs_count == 0) ? "first" : "additional")); 

					/* Create or resize the trigger pointer array to contain another pair of anchors */
   					triggerptrs = realloc(triggerptrs, (sizeof(char *) * 2) * (++triggerptrs_count));
					if (triggerptrs == NULL) return "Out of memory";

					/* Save the current triggerstartpos as our first anchor in the pair */
					triggerptrs[triggerptrs_count - 1][0] = triggerstartpos;
					triggerptrs[triggerptrs_count - 1][1] = NULL;

					if (triggerptrs_count > 1 && (triggerstartpos <= triggerptrs[triggerptrs_count - 2][1])) {
						/* Whoops! This trigger's LINES_AROUND_TRIGGER bleeds into the prior's LINES_AROUND_TRIGGER */
						triggerptrs[triggerptrs_count - 1][0] = triggerptrs[triggerptrs_count - 2][1];
						dbgprintf("Current trigger START (w/ prepended LINES_AROUND_TRIGGER) would overlap with prior trigger's END. Adjusting.\n");
					}

					dbgprintf(" - new trigger anchor START position set\n");
		               } 
		               else {
					/* Do nothing. Merge the two trigger lines into a single start and end pair by extending the existing */
					dbgprintf("Additional trigger line encountered. Previous trigger START has no set END yet. Compressing anchors.\n");
				}
			}
		}


		/* We want this line */
		lpidx = ((lpidx + 1) % (2*LINES_AROUND_TRIGGER+1));
		fillpos += strlen(fillpos);

		/* Save the current end-position if we had a trigger within the past LINES_AFTER_TRIGGER lines */
		if (triggerlinecount) {
			triggerlinecount--;
			triggerendpos = fillpos;

			if (triggerlinecount == 0) {
				/* Terminate the current trigger anchor pair by aligning the end pointer */
				dbgprintf(" - trigger END position set\n");
				triggerptrs[triggerptrs_count - 1][1] = triggerendpos;
			}
		}

		bytesleft = (bufsz - (fillpos - buf));
		// dbgprintf(" -- bytesleft: %zu\n", bytesleft);
	}

	if (triggerptrs != NULL) {
	        dbgprintf("Marked %i pairs of START and END anchors for consideration.\n", triggerptrs_count);

		/* Ensure that a premature EOF before the last trigger end postion doesn't blow up */
		if (triggerptrs[triggerptrs_count - 1][1] == NULL) triggerptrs[triggerptrs_count -1][1] = fillpos;
	}

	/* Was there an error reading the file? */
	if (ferror(fd)) {
		buf = (char *)malloc(1024 + strlen(filename));
		sprintf(buf, "Error while reading logfile %s : %s\n", filename, strerror(errno));
		startpos = buf;
		goto cleanup;
	}

	bytesread = (fillpos - startpos);
	*(buf + bytesread) = '\0';

	if (bytesread > logdef->maxbytes) {
		char *skiptxt = "<...SKIPPED...>\n";

	        if (triggerptrs != NULL) {
		       size_t triggerbytes, nontriggerbytes, skiptxtbytes;
		       char *pos;
		       size_t size;

		       /* Sum the number of bytes required to hold all the trigger content (start -> end anchors) */
		       for (i = 0, triggerbytes = 0, skiptxtbytes = 0; i < triggerptrs_count; i++) {
		           triggerbytes += strlen(triggerptrs[i][0]) - strlen(triggerptrs[i][1]);
		           skiptxtbytes += strlen(skiptxt) * 2;
		       }
	  
		       /* Find the remaining bytes allowed for non-trigger content (and prevent size_t underflow wrap ) */
			nontriggerbytes = (logdef->maxbytes < triggerbytes) ? 0 : (logdef->maxbytes - triggerbytes);

		       dbgprintf("Found %zu bytes of trigger data, %zu bytes remaining of non-trigger data. Max allowed is %zu.\n", triggerbytes, nontriggerbytes, logdef->maxbytes);

		       /* Allocate a new buffer, reduced to what we actually can hold */
		       replacement = malloc(sizeof(char) * (triggerbytes + skiptxtbytes + nontriggerbytes));
		       if (replacement == NULL) return "Out of memory";

		       dbgprintf("Staging replacement buffer, %zu bytes.\n", (triggerbytes + skiptxtbytes + nontriggerbytes));

		       /* Iterate each trigger anchor pair, copying into the replacement */
		       for (i = 0, pos = replacement; i < triggerptrs_count; i++) {
		               dbgprintf("Copying buffer content for trigger %i.\n", (i + 1));

		               strncpy(pos, skiptxt, strlen(skiptxt));
		               pos += strlen(skiptxt);

		               size = strlen(triggerptrs[i][0]) - strlen(triggerptrs[i][1]);
		               strncpy(pos, triggerptrs[i][0], size);
		               pos += size;
		       }

		       /* At this point, all the required trigger lines are present */

		       if (nontriggerbytes > 0) {
		               /* Append non-trigger, up to the allowed byte size remaining, of remaining log content */
		               dbgprintf("Copying %zu bytes of non-trigger content.\n", nontriggerbytes);

		               /* Add the final skip for completeness */
		               strncpy(pos, skiptxt, strlen(skiptxt));
		               pos += strlen(skiptxt);

		               /* And copy the the rest of the original buffer content, starting at the last trigger END */
		               strncpy(pos, triggerptrs[triggerptrs_count - 1][1], nontriggerbytes);
		       }

		       /* Prune out the last line to prevent sending a partial */
		       if (*(pos = &replacement[strlen(replacement) - 1]) != '\n') {
		               while (*pos != '\n') {
		                       pos -= 1;
		               }
		               *(++pos) = '\0';
		       }

		       startpos = replacement;
		       bytesread = strlen(startpos);          
	        }
		else {
			/* Just drop what is too much */
			startpos += (bytesread - logdef->maxbytes);
			memcpy(startpos, skiptxt, strlen(skiptxt));
			bytesread = logdef->maxbytes;
		}
	}

	/* Avoid sending a '[' as the first char on a line */
	{
		char *p;

		p = startpos;
		while (p) {
			if (*p == '[') *p = '.';
			p = strstr(p, "\n[");
			if (p) p++;
		}
	}

cleanup:
	if (fd) fclose(fd);

	{
		int i;

		if (logdef->ignorecount) {
			for (i=0; (i < logdef->ignorecount); i++) {
				if (logdef->ignore[i]) regfree(&ignexpr[i]);
			}
			xfree(ignexpr);
		}

		if (logdef->triggercount) {
			for (i=0; (i < logdef->triggercount); i++) {
				if (logdef->trigger[i]) regfree(&trigexpr[i]);
			}
			xfree(trigexpr);
		}

		if (triggerptrs) xfree(triggerptrs);
	}

	return startpos;
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
	int openerr, buflen;

        if ((ctx = digest_init(dtype)) == NULL) return "";

	fd = fileopen(fn, &openerr); 
	if (fd == NULL) return "";
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
	time_t now = getcurrenttime(NULL);

	*linknam = '\0';
	staterror = lstat(fn, &st);
	if ((staterror == 0) && S_ISLNK(st.st_mode)) {
		int n = readlink(fn, linknam, sizeof(linknam)-1);
		if (n == -1) n = 0;
		linknam[n] = '\0';
		staterror = stat(fn, &st);
	}

	if (staterror == -1) {
		fprintf(fd, "ERROR: %s\n", strerror(errno));
	}
	else {
		char *stsizefmt;

		pw = getpwuid(st.st_uid);
		gr = getgrgid(st.st_gid);

		fprintf(fd, "type:%o (%s)\n", 
			(unsigned int)(st.st_mode & S_IFMT), 
			ftypestr(st.st_mode, (*linknam ? linknam : NULL)));
		fprintf(fd, "mode:%o (%s)\n", 
			(unsigned int)(st.st_mode & (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)), 
			fmodestr(st.st_mode));
		fprintf(fd, "linkcount:%d\n", (int)st.st_nlink);
		fprintf(fd, "owner:%u (%s)\n", (unsigned int)st.st_uid, (pw ? pw->pw_name : ""));
		fprintf(fd, "group:%u (%s)\n", (unsigned int)st.st_gid, (gr ? gr->gr_name : ""));

		if (sizeof(st.st_size) == sizeof(long long int)) stsizefmt = "size:%lld\n";
		else stsizefmt = "size:%ld\n";
		fprintf(fd, stsizefmt,         st.st_size);

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

void printdirdata(FILE *fd, char *fn)
{
	char *ducmd;
	FILE *cmdfd;
	char *cmd;
	char buf[4096];
	int buflen;

	ducmd = getenv("DU");
	if (ducmd == NULL) ducmd = "du -k";

	cmd = (char *)malloc(strlen(ducmd) + strlen(fn) + 10);
	sprintf(cmd, "%s %s 2>&1", ducmd, fn);

	cmdfd = popen(cmd, "r");
	xfree(cmd);
	if (cmdfd == NULL) return;

	while ((buflen = fread(buf, 1, sizeof(buf), cmdfd)) > 0) fwrite(buf, 1, buflen, fd);
	pclose(cmdfd);

	fprintf(fd, "\n");
}

void printcountdata(FILE *fd, checkdef_t *cfg)
{
	int openerr, idx;
	FILE *logfd;
	regex_t *exprs;
	regmatch_t pmatch[1];
	int *counts;
	char l[8192];
	
	logfd = fileopen(cfg->filename, &openerr); 
	if (logfd == NULL) {
		fprintf(fd, "ERROR: Cannot open file %s: %s\n", cfg->filename, strerror(openerr));
		return;
	}

	counts = (int *)calloc(cfg->check.countcheck.patterncount, sizeof(int));
	exprs = (regex_t *)calloc(cfg->check.countcheck.patterncount, sizeof(regex_t));
	for (idx = 0; (idx < cfg->check.countcheck.patterncount); idx++) {
		int status;

		status = regcomp(&exprs[idx], cfg->check.countcheck.patterns[idx], REG_EXTENDED|REG_NOSUB);
		if (status != 0) { /* ... */ };
	}

	while (fgets(l, sizeof(l), logfd)) {
		for (idx = 0; (idx < cfg->check.countcheck.patterncount); idx++) {
			if (regexec(&exprs[idx], l, 1, pmatch, 0) == 0) counts[idx] += 1;
		}
	}

	fclose(logfd);

	for (idx = 0; (idx < cfg->check.countcheck.patterncount); idx++) {
		fprintf(fd, "%s: %d\n", 
			cfg->check.countcheck.patternnames[idx], counts[idx]);

		regfree(&exprs[idx]);
	}

	free(counts);
	free(exprs);
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
		checktype_t checktype;
		char *bol, *filename;
		int maxbytes, domd5, dosha1, dormd160;

		{ char *p = strchr(l, '\n'); if (p) *p = '\0'; }

		bol = l + strspn(l, " \t");
		if ((*bol == '\0') || (*bol == '#')) continue;

		if      (strncmp(bol, "log:", 4) == 0) checktype = C_LOG;
		else if (strncmp(bol, "file:", 5) == 0) checktype = C_FILE;
		else if (strncmp(bol, "dir:", 4) == 0) checktype = C_DIR;
		else if (strncmp(bol, "linecount:", 10) == 0) checktype = C_COUNT;
		else checktype = C_NONE;

		if (checktype != C_NONE) {
			char *tok;

			filename = NULL; maxbytes = -1; domd5 = dosha1 = dormd160 = 0;

			/* Skip the initial keyword token */
			tok = strtok(l, ":"); filename = strtok(NULL, ":");
			switch (checktype) {
			  case C_LOG:
				tok = (filename ? strtok(NULL, ":") : NULL);
				if (tok) maxbytes = atoi(tok);
				break;

			  case C_FILE:
				maxbytes = 0; /* Needed to get us into the put-into-list code */
				tok = (filename ? strtok(NULL, ":") : NULL);
				if (tok) {
					if (strcmp(tok, "md5") == 0) domd5 = 1;
					else if (strcmp(tok, "sha1") == 0) dosha1 = 1;
					else if (strcmp(tok, "rmd160") == 0) dormd160 = 1;
				}
				break;

			  case C_DIR:
				maxbytes = 0; /* Needed to get us into the put-into-list code */
				break;

			  case C_COUNT:
				maxbytes = 0; /* Needed to get us into the put-into-list code */
				break;

			  case C_NONE:
				break;
			}

			if ((filename != NULL) && (maxbytes != -1)) {
				checkdef_t *newitem;

				firstpipeitem = NULL;

				if (*filename == '`') {
					/* Run the command to get filenames */
					char *p;
					char *cmd;
					FILE *fd;

					cmd = filename+1;
					p = strchr(cmd, '`'); if (p) *p = '\0';
					fd = popen(cmd, "r");
					if (fd) {
						char pline[PATH_MAX+1];

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
							  case C_DIR:
								break;
							  case C_COUNT:
								newitem->check.countcheck.patterncount = 0;
								newitem->check.countcheck.patternnames = calloc(1, sizeof(char *));
								newitem->check.countcheck.patterns = calloc(1, sizeof(char *));
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
					  case C_DIR:
						break;
					  case C_COUNT:
						newitem->check.countcheck.patterncount = 0;
						newitem->check.countcheck.patternnames = calloc(1, sizeof(char *));
						newitem->check.countcheck.patterns = calloc(1, sizeof(char *));
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
			if (strncmp(bol, "ignore ", 7) == 0) {
				char *p; 

				p = bol + 7; p += strspn(p, " \t");

				if (firstpipeitem) {
					/* Fill in this ignore expression on all items in this pipe set */
					checkdef_t *walk = currcfg;

					do {
						walk->check.logcheck.ignorecount++;
						if (walk->check.logcheck.ignore == NULL) {
							walk->check.logcheck.ignore = (char **)malloc(sizeof(char *));
						}
						else {
							walk->check.logcheck.ignore = 
								(char **)realloc(walk->check.logcheck.ignore, 
										 walk->check.logcheck.ignorecount * sizeof(char **));
						}

						walk->check.logcheck.ignore[walk->check.logcheck.ignorecount-1] = strdup(p);

						walk = walk->next;
					} while (walk && (walk != firstpipeitem->next));
				}
				else {
					currcfg->check.logcheck.ignorecount++;
					if (currcfg->check.logcheck.ignore == NULL) {
						currcfg->check.logcheck.ignore = (char **)malloc(sizeof(char *));
					}
					else {
						currcfg->check.logcheck.ignore = 
							(char **)realloc(currcfg->check.logcheck.ignore, 
									 currcfg->check.logcheck.ignorecount * sizeof(char **));
					}

					currcfg->check.logcheck.ignore[currcfg->check.logcheck.ignorecount-1] = strdup(p);
				}
			}
			else if (strncmp(bol, "trigger ", 8) == 0) {
				char *p; 

				p = bol + 8; p += strspn(p, " \t");

				if (firstpipeitem) {
					/* Fill in this trigger expression on all items in this pipe set */
					checkdef_t *walk = currcfg;

					do {
						walk->check.logcheck.triggercount++;
						if (walk->check.logcheck.trigger == NULL) {
							walk->check.logcheck.trigger = (char **)malloc(sizeof(char *));
						}
						else {
							walk->check.logcheck.trigger = 
								(char **)realloc(walk->check.logcheck.trigger, 
										 walk->check.logcheck.triggercount * sizeof(char **));
						}

						walk->check.logcheck.trigger[walk->check.logcheck.triggercount-1] = strdup(p);

						walk = walk->next;
					} while (walk && (walk != firstpipeitem->next));
				}
				else {
					currcfg->check.logcheck.triggercount++;
					if (currcfg->check.logcheck.trigger == NULL) {
						currcfg->check.logcheck.trigger = (char **)malloc(sizeof(char *));
					}
					else {
						currcfg->check.logcheck.trigger = 
							(char **)realloc(currcfg->check.logcheck.trigger, 
									 currcfg->check.logcheck.triggercount * sizeof(char **));
					}

					currcfg->check.logcheck.trigger[currcfg->check.logcheck.triggercount-1] = strdup(p);
				}
			}
		}
		else if (currcfg && (currcfg->checktype == C_FILE)) {
			/* Nothing */
		}
		else if (currcfg && (currcfg->checktype == C_DIR)) {
			/* Nothing */
		}
		else if (currcfg && (currcfg->checktype == C_COUNT)) {
			int idx;
			char *name, *ptn = NULL;

			name = strtok(l, " :");
			if (name) ptn = strtok(NULL, "\n");
			idx = currcfg->check.countcheck.patterncount;

			if (name && ptn) {
				currcfg->check.countcheck.patterncount += 1;

				currcfg->check.countcheck.patternnames = 
					realloc(currcfg->check.countcheck.patternnames,
						(currcfg->check.countcheck.patterncount+1)*sizeof(char *));

				currcfg->check.countcheck.patterns = 
					realloc(currcfg->check.countcheck.patterns,
						(currcfg->check.countcheck.patterncount+1)*sizeof(char *));

				currcfg->check.countcheck.patternnames[idx] = strdup(name);
				currcfg->check.countcheck.patterns[idx] = strdup(ptn);
			}
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
#ifdef _LARGEFILE_SOURCE
			if (tok) walk->check.logcheck.lastpos[i] = (off_t)str2ll(tok, NULL);
#else
			if (tok) walk->check.logcheck.lastpos[i] = atol(tok);
#endif

			/* Sanity check */
			if (walk->check.logcheck.lastpos[i] < 0) walk->check.logcheck.lastpos[i] = 0;
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
		char *fmt;

		if (walk->checktype != C_LOG) continue;


		if (sizeof(walk->check.logcheck.lastpos[i]) == sizeof(long long int)) fmt = ":%lld";
		else fmt = ":%ld";

		fprintf(fd, "%s", walk->filename);
		for (i = 0; (i < POSCOUNT); i++) fprintf(fd, fmt, walk->check.logcheck.lastpos[i]);
		fprintf(fd, "\n");
	}
	fclose(fd);
}

int main(int argc, char *argv[])
{
	char *cfgfn = NULL, *statfn = NULL;
	int i;
	checkdef_t *walk;

#ifdef BIG_SECURITY_HOLE
	drop_root();
#else
	drop_root_and_removesuid(argv[0]);
#endif

	for (i=1; (i<argc); i++) {
		if (strcmp(argv[i], "--debug") == 0) {
			char *delim = strchr(argv[i], '=');
			debug = 1;
			if (delim) set_debugfile(delim+1, 0);
		}
		else if (strcmp(argv[i], "--clock") == 0) {
			struct timeval tv;
			struct timezone tz;
			struct tm *tm;
			char timestr[50];

			gettimeofday(&tv, &tz);
			printf("epoch: %ld.%06ld\n", (long int)tv.tv_sec, (long int)tv.tv_usec);

			/*
			 * OpenBSD mistakenly has struct timeval members defined as "long",
			 * but requires localtime and gmtime to have a "time_t *" argument.
			 * Figures ...
			 */
			tm = localtime((time_t *)&tv.tv_sec);
			strftime(timestr, sizeof(timestr), "local: %Y-%m-%d %H:%M:%S %Z", tm);
			printf("%s\n", timestr);

			tm = gmtime((time_t *)&tv.tv_sec);
			strftime(timestr, sizeof(timestr), "UTC: %Y-%m-%d %H:%M:%S %Z", tm);
			printf("%s\n", timestr);
			return 0;
		}
		else if ((*(argv[i]) == '-') && (strlen(argv[i]) > 1)) {
			fprintf(stderr, "Unknown option %s\n", argv[i]);
		}
		else {
			/* Not an option -- should have two arguments left: our config and status file */
			if (cfgfn == NULL) cfgfn = argv[i];
			else if (statfn == NULL) statfn = argv[i];
			else fprintf(stderr, "Unknown argument '%s'\n", argv[i]);
		}
	}

	if ((cfgfn == NULL) || (statfn == NULL)) {
		fprintf(stderr, "Missing config or status file arguments\n");
		return 1;
	}

	if (loadconfig(cfgfn) != 0) return 1;
	loadlogstatus(statfn);

	for (walk = checklist; (walk); walk = walk->next) {
		char *data;
		checkdef_t *fwalk;

		switch (walk->checktype) {
		  case C_LOG:
			data = logdata(walk->filename, &walk->check.logcheck);
			fprintf(stdout, "[msgs:%s]\n", walk->filename);
			fprintf(stdout, "%s\n", data);

			/* See if there's a special "file:" entry for this logfile */
			for (fwalk = checklist; (fwalk && ((fwalk->checktype != C_FILE) || (strcmp(fwalk->filename, walk->filename) != 0))); fwalk = fwalk->next) ;
			if (fwalk == NULL) {
				/* No specific file: entry, so make sure the logfile metadata is available */
				fprintf(stdout, "[logfile:%s]\n", walk->filename);
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

		  case C_DIR:
			fprintf(stdout, "[dir:%s]\n", walk->filename);
			printdirdata(stdout, walk->filename);
			break;

		  case C_COUNT:
			fprintf(stdout, "[linecount:%s]\n", walk->filename);
			printcountdata(stdout, walk);
			break;

		  case C_NONE:
			break;
		}
	}

	savelogstatus(statfn);

	return 0;
}

