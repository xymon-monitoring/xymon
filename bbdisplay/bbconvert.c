/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbconvert.c,v 1.3 2004-10-30 15:38:13 henrik Exp $";

#include <limits.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "bbgen.h"
#include "bbconvert.h"

static unsigned char *nlencode(unsigned char *msg)
{
	static unsigned char *buf = NULL;
	static int bufsz = 0;
	int maxneeded;
	unsigned char *inp, *outp;
	int n;

	if (msg == NULL) msg = "";

	maxneeded = 2*strlen(msg)+1;

	if (buf == NULL) {
		bufsz = maxneeded;
		buf = (char *)malloc(bufsz);
	}
	else if (bufsz < maxneeded) {
		bufsz = maxneeded;
		buf = (char *)realloc(buf, bufsz);
	}

	inp = msg;
	outp = buf;

	while (*inp) {
		n = strcspn(inp, "|\n\r\t\\");
		if (n > 0) {
			memcpy(outp, inp, n);
			outp += n;
			inp += n;
		}

		if (*inp) {
			*outp = '\\'; outp++;
			switch (*inp) {
			  case '|' : *outp = 'p'; outp++; break;
			  case '\n': *outp = 'n'; outp++; break;
			  case '\r': *outp = 'r'; outp++; break;
			  case '\t': *outp = 't'; outp++; break;
			  case '\\': *outp = '\\'; outp++; break;
			}
			inp++;
		}
	}
	*outp = '\0';

	return buf;
}

void dump_bbgendchk(void)
{
	hostlist_t *hwalk;
	entry_t *e;

	for (hwalk = hosthead; (hwalk); hwalk = hwalk->next) {
		host_t *h = hwalk->hostentry;

		for (e = h->entries; (e); e = e->next) {
			char logfn[PATH_MAX];
			struct stat st;
			FILE *logfd;
			char *logbuf, *logenc;
			int n;
			char *flags = NULL;
			char *sender = NULL;
			char *unchstr = NULL;
			char *p;
			time_t validtime;
			int oldcol = -1;
			time_t lastchange = 0;
			time_t enabletime = 0;
			time_t acktime = 0;
			time_t logtime = 0;
			int cookie = -1;
			time_t cookieexpires = 0;

			sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), commafy(h->hostname), e->column->name);
			if (stat(logfn, &st) == -1) continue;
			logfd = fopen(logfn, "r");
			if (logfd == NULL) continue;
			logbuf = (char *)malloc(st.st_size+1);
			n = fread(logbuf, 1, st.st_size, logfd);
			fclose(logfd);
			if (n == -1) {
				free(logbuf);
				continue;
			}
			*(logbuf+n) = '\0';

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

			sprintf(logfn, "%s/%s.%s", getenv("BBHIST"), commafy(h->hostname), e->column->name);
			stat(logfn, &st);
			logfd = fopen(logfn, "r");
			if (logfd) {
				char l[100], colstr[20];
				int curcol = COL_GREEN, n;
				if (st.st_size > 130) fseek(logfd, -130, SEEK_END);
				while (fgets(l, sizeof(l), logfd)) {
					n = sscanf(l+25, "%s %d", colstr, &lastchange);
					if (n == 2) {
						oldcol = curcol;
						curcol = parse_color(colstr);
					}
				}
			}
			fclose(logfd);

			sprintf(logfn, "%s/%s.%s", getenv("BBDISABLED"), commafy(h->hostname), e->column->name);
			if (stat(logfn, &st) == 0) enabletime = st.st_mtime;

			printf("@@BBGENDCHK-V1|%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%s",
				h->hostname, e->column->name, sender,
				colorname(e->color),
				(flags ? flags : ""),
				colorname(oldcol),
				(int) logtime, (int) lastchange, (int) validtime,
				(int) enabletime, (int) acktime,
				cookie, (int) cookieexpires,
				logenc);
			printf("|%s", ""); /* disable msg */
			printf("|%s", ""); /* ack msg */
			printf("\n");
		}
	}
}

