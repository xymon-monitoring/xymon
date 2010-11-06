/*----------------------------------------------------------------------------*/
/* Xymon history log trimming tool.                                           */
/*                                                                            */
/* This tool trims the history-logs of old entries.                           */
/*                                                                            */
/* Copyright (C) 2005-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <utime.h>
#include <limits.h>
#include <signal.h>

#include "libxymon.h"

enum ftype_t { F_HOSTHISTORY, F_SERVICEHISTORY, F_ALLEVENTS, F_DROPIT, F_PURGELOGS };
typedef struct filelist_t {
	char *fname;
	enum ftype_t ftype;
	struct filelist_t *next;
} filelist_t;
filelist_t *flhead = NULL;

char *outdir = NULL;
int progressinfo = 0;
int totalitems = 0;

void showprogress(int itemno)
{
	errprintf("Processing item %d/%d ... \n", itemno, totalitems);
}

int validstatus(char *hname, char *tname)
{
	/* Check if a status-file is for a known host+service combination */
	static char *board = NULL;
	char buf[1024];
	char *p;
	int result = 0;


	if (!board) {
		sendreturn_t *sres;

		sres = newsendreturnbuf(1, NULL);
		if (sendmessage("hobbitdboard fields=hostname,testname", NULL, XYMON_TIMEOUT, sres) != XYMONSEND_OK) {
			errprintf("Cannot get list of host/test combinations\n");
			exit(1);
		}
		board = getsendreturnstr(sres, 1);
		freesendreturnbuf(sres);

		if (debug) {
			char fname[PATH_MAX];
			FILE *fd;

			sprintf(fname, "%s/board.dbg", xgetenv("XYMONTMP"));
			fd = fopen(fname, "w");
			if (fd) {
				fwrite(board, strlen(board), 1, fd);
				fclose(fd);
			}
			else {
				errprintf("Cannot open debug file %s: %s\n", fname, strerror(errno));
			}
		}
	}

	sprintf(buf, "%s|%s\n", hname, tname);
	p = strstr(board, buf);
	if (p) result = ( (p == board) || (*(p-1) == '\n'));

	return result;
}

void add_to_filelist(char *fn, enum ftype_t ftype)
{
	/* This keeps track of what files we must process - and how */
	filelist_t *newitem;

	newitem = (filelist_t *)malloc(sizeof(filelist_t));
	newitem->fname = strdup(fn);
	newitem->ftype = ftype;
	newitem->next = flhead;
	flhead = newitem;

	totalitems++;
}


void trim_history(FILE *infd, FILE *outfd, enum ftype_t ftype, time_t cutoff)
{
	/* Does the grunt work of going through a file and copying the wanted records */
	char l[4096], prevl[4096], l2[4096];
	char *cols[10];
	int i;
	int copying = 0;

	*prevl = '\0';

	while (fgets(l, sizeof(l), infd)) {
		if (copying) {
			fprintf(outfd, "%s", l);
		}
		else {
			/* Split up the input line into columns, and find the timestamp depending on the file type */
			memset(cols, 0, sizeof(cols));
			strcpy(l2, l); i = 0; cols[i++] = strtok(l2, " "); 
			while ((i < 10) && ((cols[i++] = strtok(NULL, " ")) != NULL)) ;

			switch (ftype) {
			  case F_HOSTHISTORY:
				copying = (!cols[1] || (atoi(cols[1]) >= cutoff));
				break;

			  case F_SERVICEHISTORY:
				copying = (!cols[6] || (atoi(cols[6]) >= cutoff));
				break;

			  case F_ALLEVENTS:
				copying = (!cols[3] || (atoi(cols[3]) >= cutoff));
				break;

			  case F_DROPIT:
			  case F_PURGELOGS:
				/* Cannot happen */
				errprintf("Impossible - F_DROPIT/F_PURGELOGS in trim_history\n");
				return;
			}

			/* If we switched to copy-mode, start by outputting the previous and the current lines */
			if (copying) {
				if (*prevl) fprintf(outfd, "%s", prevl);
				fprintf(outfd, "%s", l);
			}
			else {
				strcpy(prevl, l);
			}
		}
	}

	if (!copying) {
		/* No entries after the cutoff time - keep the last line */
		if (*prevl) fprintf(outfd, "%s", prevl);
	}
}

void trim_files(time_t cutoff)
{
	filelist_t *fwalk;
	FILE *infd, *outfd;
	char outfn[PATH_MAX];
	struct stat st;
	struct utimbuf tstamp;
	int itemno = 0;

	/* We have a list of files to trim, so process them */
	for (fwalk = flhead; (fwalk); fwalk = fwalk->next) {
		dbgprintf("Processing %s\n", fwalk->fname);
		itemno++; if (progressinfo && ((itemno % progressinfo) == 0)) showprogress(itemno); 

		if (fwalk->ftype == F_DROPIT) {
			/* It's an orphan, and we want to delete it */
			unlink(fwalk->fname); 
			continue;
		}

		if (stat(fwalk->fname, &st) == -1) {
			errprintf("Cannot stat input file %s: %s\n", fwalk->fname, strerror(errno));
			continue;
		}
		tstamp.actime = time(NULL);
		tstamp.modtime = st.st_mtime;

		infd = fopen(fwalk->fname, "r");
		if (infd == NULL) {
			errprintf("Cannot open input file %s: %s\n", fwalk->fname, strerror(errno));
			continue;
		}

		if (outdir) {
			sprintf(outfn, "%s/%s", outdir, fwalk->fname);
		}
		else {
			sprintf(outfn, "%s.tmp", fwalk->fname);
		}
		outfd = fopen(outfn, "w");
		if (outfd == NULL) {
			errprintf("Cannot create output file %s: %s\n", outfn, strerror(errno));
			fclose(infd);
			continue;
		}

		trim_history(infd, outfd, fwalk->ftype, cutoff);
		if (fwalk->ftype == F_ALLEVENTS) {
			char pidfn[PATH_MAX];
			FILE *fd;
			long pid = -1;

			sprintf(pidfn, "%s/xymond_history.pid", xgetenv("XYMONSERVERLOGS"));
			fd = fopen(pidfn, "r");
			if (fd) {
				char l[100];
				fgets(l, sizeof(l), fd);
				fclose(fd);
				pid = atol(l);
			}

			if (pid > 0) kill(pid, SIGHUP);
		}

		fclose(infd);
		fclose(outfd);
		utime(outfn, &tstamp);	/* So the access time is consistent with the last update */

		/* Final check to make sure the file didn't change while we were processing it */
		if ((stat(fwalk->fname, &st) == 0) && (st.st_mtime == tstamp.modtime)) {
			if (!outdir) rename(outfn, fwalk->fname);
		}
		else {
			errprintf("File %s changed while processing it - not trimmed\n", fwalk->fname);
			unlink(outfn);
		}
	}
}

time_t logtime(char *fn)
{
	static char *mnames[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };
	char tstamp[25];
	struct tm tmstamp;
	time_t result;
	int flen;

	flen = strlen(fn);
	strcpy(tstamp, fn);
	memset(&tmstamp, 0, sizeof(tmstamp));
	tmstamp.tm_isdst = -1;

	if (flen == 24) {
		/* fn is of the form: WWW_MMM_DD_hh:mm:ss_YYYY */
		if (*(tstamp+3) == '_')  *(tstamp+3) = ' '; else return -1;
		if (*(tstamp+7) == '_')  *(tstamp+7) = ' '; else return -1;
		if (*(tstamp+10) == '_') *(tstamp+10) = ' '; else return -1;
		if (*(tstamp+13) == ':') *(tstamp+13) = ' '; else return -1;
		if (*(tstamp+16) == ':') *(tstamp+16) = ' '; else return -1;
		if (*(tstamp+19) == '_') *(tstamp+19) = ' '; else return -1;

		while (mnames[tmstamp.tm_mon] && strncmp(tstamp+4, mnames[tmstamp.tm_mon], 3)) tmstamp.tm_mon++;
		tmstamp.tm_mday  = atoi(tstamp+8);
		tmstamp.tm_year  = atoi(tstamp+20)-1900;
		tmstamp.tm_hour  = atoi(tstamp+11);
		tmstamp.tm_min   = atoi(tstamp+14);
		tmstamp.tm_sec   = atoi(tstamp+17);
	}
	else if (flen == 23) {
		/* fn is of the form: WWW_MMM_D_hh:mm:ss_YYYY */
		if (*(tstamp+3) == '_')  *(tstamp+3) = ' '; else return -1;
		if (*(tstamp+7) == '_')  *(tstamp+7) = ' '; else return -1;
		if (*(tstamp+9) == '_')  *(tstamp+9) = ' '; else return -1;
		if (*(tstamp+12) == ':') *(tstamp+12) = ' '; else return -1;
		if (*(tstamp+15) == ':') *(tstamp+15) = ' '; else return -1;
		if (*(tstamp+18) == '_') *(tstamp+18) = ' '; else return -1;

		while (mnames[tmstamp.tm_mon] && strncmp(tstamp+4, mnames[tmstamp.tm_mon], 3)) tmstamp.tm_mon++;
		tmstamp.tm_mday  = atoi(tstamp+8);
		tmstamp.tm_year  = atoi(tstamp+19)-1900;
		tmstamp.tm_hour  = atoi(tstamp+10);
		tmstamp.tm_min   = atoi(tstamp+13);
		tmstamp.tm_sec   = atoi(tstamp+16);
	}
	else {
		return -1;
	}

	result = mktime(&tmstamp);

	return result;
}

void trim_logs(time_t cutoff)
{
	filelist_t *fwalk;
	DIR *ldir = NULL, *sdir = NULL;
	struct dirent *sent, *lent;
	time_t ltime;
	char fn1[PATH_MAX], fn2[PATH_MAX];
	int itemno = 0;

	/* We have a list of directories to trim, so process them */
	for (fwalk = flhead; (fwalk); fwalk = fwalk->next) {
		dbgprintf("Processing %s\n", fwalk->fname);
		itemno++; if (progressinfo && ((itemno % progressinfo) == 0)) showprogress(itemno); 

		switch (fwalk->ftype) {
		  case F_DROPIT:
			/* It's an orphan, and we want to delete it */
			dropdirectory(fwalk->fname, 0);
			break;

		  case F_PURGELOGS:
			sdir = opendir(fwalk->fname);
			if (sdir == NULL) {
				errprintf("Cannot process directory %s: %s\n", fwalk->fname, strerror(errno));
				break;
			}

			while ((sent = readdir(sdir)) != NULL) {
				int allgone = 1;
				if (*(sent->d_name) == '.') continue;

				sprintf(fn1, "%s/%s", fwalk->fname, sent->d_name);
				ldir = opendir(fn1);
				if (ldir == NULL) {
					errprintf("Cannot process directory %s: %s\n", fn1, strerror(errno));
					continue;
				}

				while ((lent = readdir(ldir)) != NULL) {
					if (*(lent->d_name) == '.') continue;

					ltime = logtime(lent->d_name);
					if ((ltime > 0) && (ltime < cutoff)) {
						sprintf(fn2, "%s/%s", fn1, lent->d_name);
						if (unlink(fn2) == -1) {
							errprintf("Failed to unlink %s: %s\n", fn2, strerror(errno));
						}
					}
					else allgone = 0;
				}

				closedir(ldir);

				/* Is it empty ? Then remove it */
				if (allgone) rmdir(fn1);
			}

			closedir(sdir);
			break;

		  default:
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	int argi;
	DIR *histdir = NULL;
	struct dirent *hent;
	struct stat st;
	time_t cutoff = 0;
	int dropsvcs = 0;
	int dropfiles = 0;
	int droplogs = 0;
	char *envarea = NULL;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--cutoff=")) {
			char *p = strchr(argv[argi], '=');
			cutoff = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--outdir=")) {
			char *p = strchr(argv[argi], '=');
			outdir = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--drop") == 0) {
			dropfiles = 1;
		}
		else if (strcmp(argv[argi], "--dropsvcs") == 0) {
			dropsvcs = 1;
		}
		else if (strcmp(argv[argi], "--droplogs") == 0) {
			droplogs = 1;
		}
		else if (strcmp(argv[argi], "--progress") == 0) {
			progressinfo = 100;
		}
		else if (argnmatch(argv[argi], "--progress=")) {
			char *p = strchr(argv[argi], '=');
			progressinfo = atoi(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			printf("Usage:\n\n\t%s --cutoff=TIME\n\nTIME is in seconds since epoch\n", argv[0]);
			return 0;
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
	}

	if (cutoff == 0) {
		errprintf("Must have a cutoff-time\n");
		return 1;
	}

	if (chdir(xgetenv("XYMONHISTDIR")) == -1) {
		errprintf("Cannot cd to history directory: %s\n", strerror(errno));
		return 1;
	}

	histdir = opendir(".");
	if (!histdir) {
		errprintf("Cannot read history directory: %s\n", strerror(errno));
		return 1;
	}

	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());

	/* First scan the directory for all files, and pick up the ones we want to process */
	while ((hent = readdir(histdir)) != NULL) {
		char *hostname = NULL;
		char hostip[IP_ADDR_STRLEN];
		enum ghosthandling_t ghosthandling = GH_IGNORE;

		if (stat(hent->d_name, &st) == -1) {
			errprintf("Odd entry %s - cannot stat: %s\n", hent->d_name, strerror(errno));
			continue;
		}

		if ((*(hent->d_name) == '.') || S_ISDIR(st.st_mode)) continue;

		if (strcmp(hent->d_name, "allevents") == 0) {
			/* Special all-hosts-services event log */
			add_to_filelist(hent->d_name, F_ALLEVENTS);
			continue;
		}

		hostname = knownhost(hent->d_name, hostip, ghosthandling);
		if (hostname) {
			/* Host history file. */
			add_to_filelist(hent->d_name, F_HOSTHISTORY);
		}
		else {
			char *delim, *p, *hname, *tname;

			delim = strrchr(hent->d_name, '.');
			if (!delim) {
				/* It's a host history file (no dot in filename), but the host does not exist */
				errprintf("Orphaned host-history file %s - no host\n", hent->d_name);
				if (dropfiles) add_to_filelist(hent->d_name, F_DROPIT);
				continue;
			}

			*delim = '\0'; hname = strdup(hent->d_name); tname = delim+1; *delim = '.';
			p = strchr(hname, ','); while (p) { *p = '.'; p = strchr(p, ','); }
			hostname = knownhost(hname, hostip, ghosthandling);
			if (!hostname) {
				errprintf("Orphaned service-history file %s - no host\n", hent->d_name);
				if (dropfiles) add_to_filelist(hent->d_name, F_DROPIT);
			}
			else if (dropsvcs && !validstatus(hostname, tname)) {
				errprintf("Orphaned service-history file %s - no service\n", hent->d_name);
				if (dropfiles) add_to_filelist(hent->d_name, F_DROPIT);
			}
			else {
				/* Service history file */
				add_to_filelist(hent->d_name, F_SERVICEHISTORY);
			}
			xfree(hname);
		}
	}

	closedir(histdir);

	/* Then process the files */
	if (progressinfo) errprintf("Starting trim of %d history-logs\n", totalitems);
	trim_files(cutoff);


	/* Process statuslogs also ? */
	if (!droplogs) return 0;

	flhead = NULL;  /* Dirty - we should clean it up properly - but I dont care */
	totalitems = 0;
	if (chdir(xgetenv("XYMONHISTLOGS")) == -1) {
		errprintf("Cannot cd to historical statuslogs directory: %s\n", strerror(errno));
		return 1;
	}

	histdir = opendir(".");
	if (!histdir) {
		errprintf("Cannot read historical statuslogs directory: %s\n", strerror(errno));
		return 1;
	}

	while ((hent = readdir(histdir)) != NULL) {
		if (stat(hent->d_name, &st) == -1) {
			errprintf("Odd entry %s - cannot stat: %s\n", hent->d_name, strerror(errno));
			continue;
		}

		if ((*(hent->d_name) == '.') || !S_ISDIR(st.st_mode)) continue;

		if (knownloghost(hent->d_name)) {
			add_to_filelist(hent->d_name, F_PURGELOGS);
		}
		else {
			add_to_filelist(hent->d_name, F_DROPIT);
		}
	}

	closedir(histdir);

	if (progressinfo) errprintf("Starting trim of %d status-log collections\n", totalitems);
	trim_logs(cutoff);

	return 0;
}

