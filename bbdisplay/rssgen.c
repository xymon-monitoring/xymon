/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This file contains code to generate RSS/RDF format output of alerts.       */
/* It is heavily influenced by Jeff Stoner's bb_content-feed script.          */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: rssgen.c,v 1.1 2003-08-11 15:38:57 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bbgen.h"
#include "util.h"
#include "rssgen.h"

char *rssfilename = NULL;
char *rssversion = "0.91";

#define RSS091 0
#define RSS092 1
#define RSS10  2
#define RSS20  3

void do_rss_feed(void)
{
	FILE *fd;
	char tmpfn[MAX_PATH];
	int rssver = 0;
	int ttlvalue;
	hostlist_t *h;
	int anyshown;

	if (rssfilename == NULL) return;

	if      (strcmp(rssversion, "0.91") == 0) rssver = RSS091;
	else if (strcmp(rssversion, "0.92") == 0) rssver = RSS092;
	else if (strcmp(rssversion, "1.0") == 0)  rssver = RSS10;
	else if (strcmp(rssversion, "2.0") == 0)  rssver = RSS20;
	else {
		errprintf("Unknown RSS version requested (%s), using 0.91\n", rssversion);
		rssver = RSS091;
	}

	ttlvalue = (getenv("BBSLEEP") ? (atoi(getenv("BBSLEEP")) / 60) : 5);

	sprintf(tmpfn, "%s.tmp", rssfilename);
	fd = fopen(tmpfn, "w");
	if (fd == NULL) {
		errprintf("Cannot create RSS/RDF outputfile %s\n", tmpfn);
		return;
	}

	switch (rssver) {
	  case RSS091:
		fprintf(fd, "<?xml version=\"1.0\"?>\n");
		fprintf(fd, "<rss version=\"0.91\">\n");
		fprintf(fd, "<channel>\n");
		fprintf(fd, "  <title>Big Brother Critical Alerts</title>\n");
		fprintf(fd, "  <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "  <description>Last updated on %s</description>\n", timestamp);
		break;
	  case RSS092:
		fprintf(fd, "<?xml version=\"1.0\"?>\n");
		fprintf(fd, "<rss version=\"0.92\">\n");
		fprintf(fd, "<channel>\n");
		fprintf(fd, "  <title>Big Brother Critical Alerts</title>\n");
		fprintf(fd, "  <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "  <description>Last updated on %s</description>\n", timestamp);
		fprintf(fd, "  <image>\n");
		fprintf(fd, "    <url>%s/gifs/bblogo.gif</url>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "    <title>Big Brother</title>\n");
		fprintf(fd, "    <link>http://bb4.com</link>\n");
		fprintf(fd, "  </image>\n");
		break;
	  case RSS10:
		fprintf(fd, "<?xml version=\"1.0\"?>\n");
		fprintf(fd, "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" xmlns=\"http://purl.org/rss/1.0/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n");
		fprintf(fd, "  <channel rdf:about=\"%s\">\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "    <title>Big Brother Critical Alerts</title>\n");
		fprintf(fd, "    <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "    <description>Last updated on %s</description>\n", timestamp);
		fprintf(fd, "  </channel>\n");
		break;
	  case RSS20:
		fprintf(fd, "<?xml version=\"1.0\"?>\n");
		fprintf(fd, "<rss version=\"2.0\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n");
		fprintf(fd, "  <channel>\n");
		fprintf(fd, "    <title>Big Brother Critical Alerts</title>\n");
		fprintf(fd, "    <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "    <description>Last updated on %s</description>\n", timestamp);
		fprintf(fd, "    <ttl>%d</ttl>\n", ttlvalue);
		break;
	}

	for (h=hosthead, anyshown=0; (h); h=h->next) {
		entry_t *e;

		if (h->hostentry->color == COL_RED) {
			for (e=h->hostentry->entries; (e); e=e->next) {
				if (e->color == COL_RED) {
					anyshown = 1;

					switch (rssver) {
					  case RSS091:
					  case RSS092:
					  case RSS20:
						fprintf(fd, "  <item>\n");
						break;

					  case RSS10:
						fprintf(fd, "  <item rdf:about=\"%s/bb2.html\">\n",
							getenv("BBWEBHOSTURL"));
						break;
					}

					fprintf(fd, "    <title>%s (%s)</title>\n",
						h->hostentry->hostname, e->column->name);

					fprintf(fd, "    <link>");
					if (generate_static()) {
						fprintf(fd, "%s/html/%s.%s.html",
							getenv("BBWEBHOSTURL"), 
							h->hostentry->hostname, 
							e->column->name);
					}
					else {
						fprintf(fd, "%s%s/bb-hostsvc.sh?HOSTSVC=%s.%s",
							getenv("BBWEBHOST"),
							getenv("CGIBINURL"),
							commafy(h->hostentry->hostname), 
							e->column->name);
					}
					fprintf(fd, "</link>\n");

					fprintf(fd, "  </item>\n");
				}
			}
		}
	}

	if (!anyshown) {
		fprintf(fd, "  <item>\n");
		fprintf(fd, "    <title>No Critical Alerts</title>\n");
		fprintf(fd, "    <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "  </item>\n");
	}

	switch (rssver) {
	  case RSS091:
	  case RSS092:
	  case RSS20:
		fprintf(fd, "  </channel>\n");
		fprintf(fd, "</rss>\n");
		break;
	  case RSS10:
		fprintf(fd, "</rdf:RDF>\n");
		break;
	}

	fclose(fd);
	if (rename(tmpfn, rssfilename) != 0) {
		errprintf("Cannot move file %s to destination %s\n", tmpfn, rssfilename);
	}

	return;
}

