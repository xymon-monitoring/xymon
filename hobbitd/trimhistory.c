/*----------------------------------------------------------------------------*/
/* Hobbit history log trimming tool.                                          */
/*                                                                            */
/* This tool trims the history-logs of old entries.                           */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: trimhistory.c,v 1.1 2005-03-30 08:43:29 henrik Exp $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <utime.h>

#include "libbbgen.h"

enum ftype_t { F_HOSTHISTORY, F_SERVICEHISTORY, F_ALLEVENTS, F_DROPIT };
typedef struct filelist_t {
	char *fname;
	enum ftype_t ftype;
	struct filelist_t *next;
} filelist_t;
filelist_t *flhead = NULL;

char *outdir = NULL;

int validstatus(char *hname, char *tname)
{
	/* Check if a status-file is for a known host+service combination */
	static char *board = NULL;
	char buf[1024];
	char *p;
	int result = 0;

	if (!board) {
		if (sendmessage("hobbitdboard fields=hostname,testname", NULL, NULL, &board, 1, BBTALK_TIMEOUT) != BB_OK) {
			errprintf("Cannot get list of host/test combinations\n");
			exit(1);
		}

		if (debug) {
			FILE *fd;

			fd = fopen("/tmp/board.dbg", "w");
			fwrite(board, strlen(board), 1, fd);
			fclose(fd);
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

	/* We have a list of files to trim, so process them */
	for (fwalk = flhead; (fwalk); fwalk = fwalk->next) {
		dprintf("Processing %s\n", fwalk->fname);

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

int main(int argc, char *argv[])
{
	int argi;
	DIR *histdir = NULL;
	struct dirent *hent;
	struct stat st;
	time_t cutoff = 0;
	int dropsvcs = 0;
	int dropfiles = 0;

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
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (strcmp(argv[argi], "--help") == 0) {
			printf("Usage:\n\n\t%s --cutoff=TIME\n\nTIME is in seconds since epoch\n", argv[0]);
			return 0;
		}
	}

	if (cutoff == 0) {
		errprintf("Must have a cutoff-time\n");
		return 1;
	}

	if (chdir(xgetenv("BBHIST")) == -1) {
		errprintf("Cannot cd to history directory: %s\n", strerror(errno));
		return 1;
	}

	histdir = opendir(".");
	if (!histdir) {
		errprintf("Cannot read history directory: %s\n", strerror(errno));
		return 1;
	}

	load_hostnames(xgetenv("BBHOSTS"), NULL, get_fqdn(), NULL);

	/* First scan the directory for all files, and pick up the ones we want to process */
	while ((hent = readdir(histdir)) != NULL) {
		char *hostname = NULL;
		char hostip[20];
		int ghosthandling = 1, maybedown;

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

		hostname = knownhost(hent->d_name, hostip, ghosthandling, &maybedown);
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
			hostname = knownhost(hname, hostip, ghosthandling, &maybedown);
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
	trim_files(cutoff);

	return 0;
}

