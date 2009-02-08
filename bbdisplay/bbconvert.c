/*----------------------------------------------------------------------------*/
/* Hobbit checkpoint file generator.                                          */
/*                                                                            */
/* This file contains code to generate a Hobbit style "checkpoint" file from  */
/* the current BB-style status data. It is used by bbgen's hobbitddump option */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbconvert.c,v 1.13 2006-05-03 21:12:33 henrik Exp $";

#include <limits.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "bbgen.h"
#include "bbconvert.h"
#include "util.h"

void dump_hobbitdchk(void)
{
	hostlist_t *hwalk;
	entry_t *e;

	for (hwalk = hostlistBegin(); (hwalk); hwalk = hostlistNext()) {
		host_t *h = hwalk->hostentry;

		for (e = h->entries; (e); e = e->next) {
			char logfn[PATH_MAX];
			struct stat st;
			FILE *logfd;
			char *logbuf, *logenc;
			int n;
			size_t bytesread;
			char *flags = NULL;
			char *sender = NULL;
			char *unchstr = NULL;
			char *p;
			time_t validtime;
			int oldcol = -1;
			int lastchange = 0;
			time_t enabletime = 0;
			time_t acktime = 0;
			time_t logtime = 0;
			int cookie = -1;
			time_t cookieexpires = 0;

			sprintf(logfn, "%s/%s.%s", xgetenv("BBLOGS"), commafy(h->hostname), e->column->name);
			if (stat(logfn, &st) == -1) continue;
			logfd = fopen(logfn, "r");
			if (logfd == NULL) continue;
			logbuf = (char *)malloc(st.st_size+1);
			bytesread = fread(logbuf, 1, st.st_size, logfd);
			fclose(logfd);
			if (bytesread == -1) {
				xfree(logbuf);
				continue;
			}
			*(logbuf+bytesread) = '\0';

			logenc = nlencode(logbuf);
			validtime = st.st_mtime;
			logtime = st.st_mtime - 300;	/* Guess */

			p = strstr(logbuf, " <!-- [flags:");
			if (p) p = p + strlen(" <!-- [flags:");
			if (p) {
				char savech;
				n = strspn(p, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
				savech = *(p+n);
				*(p+n) = '\0';
				flags = strdup(p);
				*(p+n) = savech;
			}

			p = strstr(logbuf, "\nMessage received from ");
			if (p) sender = p + strlen("\nMessage received from ");
			p = strstr(logbuf, "\nStatus unchanged in ");
			if (p) unchstr = p + strlen("\nStatus unchanged in ");
			if (sender) { p = strchr(sender, '\n'); if (p) *p = '\0'; } else sender = "";
			if (unchstr) { p = strchr(unchstr, '\n'); if (p) *p = '\0'; } else unchstr = "";

			sprintf(logfn, "%s/%s.%s", xgetenv("BBHIST"), commafy(h->hostname), e->column->name);
			stat(logfn, &st);
			logfd = fopen(logfn, "r");
			if (logfd) {
				char l[100], colstr[20];
				int curcol = COL_GREEN, n;
				if (st.st_size > 130) fseeko(logfd, -130, SEEK_END);
				while (fgets(l, sizeof(l), logfd)) {
					n = sscanf(l+25, "%s %d", colstr, &lastchange);
					if (n == 2) {
						oldcol = curcol;
						curcol = parse_color(colstr);
					}
				}
			}
			fclose(logfd);

			sprintf(logfn, "%s/%s.%s", xgetenv("BBDISABLED"), commafy(h->hostname), e->column->name);
			if (stat(logfn, &st) == 0) enabletime = st.st_mtime;

			printf("@@HOBBITDCHK-V1|%s|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%s",
				"", h->hostname, e->column->name, sender,
				colorname(e->color),
				(flags ? flags : ""),
				colorname(oldcol),
				(int) logtime, lastchange, (int) validtime,
				(int) enabletime, (int) acktime,
				cookie, (int) cookieexpires,
				logenc);
			printf("|%s", ""); /* disable msg */
			printf("|%s", ""); /* ack msg */
			printf("\n");
		}
	}
}

