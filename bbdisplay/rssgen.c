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

static char rcsid[] = "$Id: rssgen.c,v 1.5 2004-08-09 09:50:14 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bbgen.h"
#include "util.h"
#include "rssgen.h"

char *rssversion = "0.91";
int  rsscolorlimit = COL_RED;
int  nssidebarcolorlimit = COL_RED;
char *rsstitle = "Big Brother Critical Alerts";

#define RSS091 0
#define RSS092 1
#define RSS10  2
#define RSS20  3

void do_rss_feed(char *rssfilename, host_t *hosts)
{
	FILE *fd;
	char tmpfn[MAX_PATH];
	char destfn[MAX_PATH];
	int rssver = 0;
	int ttlvalue;
	host_t *h;
	int anyshown;

	if (rssfilename == NULL) return;

	if (getenv("BBRSSTITLE")) rsstitle = malcop(getenv("BBRSSTITLE"));

	if      (strcmp(rssversion, "0.91") == 0) rssver = RSS091;
	else if (strcmp(rssversion, "0.92") == 0) rssver = RSS092;
	else if (strcmp(rssversion, "1.0") == 0)  rssver = RSS10;
	else if (strcmp(rssversion, "2.0") == 0)  rssver = RSS20;
	else {
		errprintf("Unknown RSS version requested (%s), using 0.91\n", rssversion);
		rssver = RSS091;
	}

	ttlvalue = (getenv("BBSLEEP") ? (atoi(getenv("BBSLEEP")) / 60) : 5);

	if (*rssfilename == '/') {
		/* Absolute filename */
		sprintf(tmpfn, "%s.tmp", rssfilename);
		sprintf(destfn, "%s", rssfilename);
	}
	else {
		/* Filename is relative to $BBHOME/www/ */
		sprintf(tmpfn, "%s/www/%s.tmp", getenv("BBHOME"), rssfilename);
		sprintf(destfn, "%s/www/%s", getenv("BBHOME"), rssfilename);
	}

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
		fprintf(fd, "  <title>%s</title>\n", rsstitle);
		fprintf(fd, "  <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "  <description>Last updated on %s</description>\n", timestamp);
		break;
	  case RSS092:
		fprintf(fd, "<?xml version=\"1.0\"?>\n");
		fprintf(fd, "<rss version=\"0.92\">\n");
		fprintf(fd, "<channel>\n");
		fprintf(fd, "  <title>%s</title>\n", rsstitle);
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
		fprintf(fd, "    <title>%s</title>\n", rsstitle);
		fprintf(fd, "    <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "    <description>Last updated on %s</description>\n", timestamp);
		fprintf(fd, "  </channel>\n");
		break;
	  case RSS20:
		fprintf(fd, "<?xml version=\"1.0\"?>\n");
		fprintf(fd, "<rss version=\"2.0\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n");
		fprintf(fd, "  <channel>\n");
		fprintf(fd, "    <title>%s</title>\n", rsstitle);
		fprintf(fd, "    <link>%s</link>\n", getenv("BBWEBHOSTURL"));
		fprintf(fd, "    <description>Last updated on %s</description>\n", timestamp);
		fprintf(fd, "    <ttl>%d</ttl>\n", ttlvalue);
		break;
	}

	for (h=hosts, anyshown=0; (h); h=h->next) {
		entry_t *e;

		if (h->color >= rsscolorlimit) {
			for (e=h->entries; (e); e=e->next) {
				if (e->color >= rsscolorlimit) {
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
						h->hostname, e->column->name);

					fprintf(fd, "    <link>");
					if (generate_static()) {
						fprintf(fd, "%s/html/%s.%s.html",
							getenv("BBWEBHOSTURL"), 
							h->hostname, 
							e->column->name);
					}
					else {
						fprintf(fd, "%s%s/bb-hostsvc.sh?HOSTSVC=%s.%s",
							getenv("BBWEBHOST"),
							getenv("CGIBINURL"),
							commafy(h->hostname), 
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
	if (rename(tmpfn, destfn) != 0) {
		errprintf("Cannot move file %s to destination %s\n", tmpfn, destfn);
	}

	return;
}

void do_netscape_sidebar(char *nssidebarfilename, host_t *hosts)
{
	FILE *fd;
	char tmpfn[MAX_PATH];
	char destfn[MAX_PATH];
	int ttlvalue;
	host_t *h;
	int anyshown;

	if (nssidebarfilename == NULL) return;

	if (getenv("BBRSSTITLE")) rsstitle = malcop(getenv("BBRSSTITLE"));

	ttlvalue = (getenv("BBSLEEP") ? atoi(getenv("BBSLEEP")) : 300);

	if (*nssidebarfilename == '/') {
		sprintf(tmpfn, "%s.tmp", nssidebarfilename);
		sprintf(destfn, "%s", nssidebarfilename);
	}
	else {
		sprintf(tmpfn, "%s/www/%s.tmp", getenv("BBHOME"), nssidebarfilename);
		sprintf(destfn, "%s/www/%s", getenv("BBHOME"), nssidebarfilename);
	}
	fd = fopen(tmpfn, "w");
	if (fd == NULL) {
		errprintf("Cannot create Netscape sidebar outputfile %s\n", tmpfn);
		return;
	}

	fprintf(fd, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">\n");
	fprintf(fd, "<HTML>\n");
	fprintf(fd, "  <HEAD>\n");
	fprintf(fd, "    <TITLE>%s</TITLE>\n", rsstitle);
	fprintf(fd, "    <META NAME=\"Generator\" CONTENT=\"bbgen - generator for Big Brother\">\n");
	fprintf(fd, "    <META HTTP-EQUIV=\"Refresh\" CONTENT=\"%d; URL=%s/%s\">\n",
		ttlvalue, getenv("BBWEBHOSTURL"), nssidebarfilename);
	fprintf(fd, "  </HEAD>\n");
	fprintf(fd, "  <BODY>\n");
	fprintf(fd, "    <FONT SIZE=\"-2\">Last updated:<BR>%s<BR></FONT>\n", timestamp);
	fprintf(fd, "    <UL>\n");

	for (h=hosts, anyshown=0; (h); h=h->next) {
		entry_t *e;

		if (h->color >= nssidebarcolorlimit) {
			for (e=h->entries; (e); e=e->next) {
				if (e->color >= nssidebarcolorlimit) {
					anyshown = 1;

					fprintf(fd, "      <LI>\n");
					if (generate_static()) {
						fprintf(fd, "\t<A TARGET=\"_content\" HREF=\"%s/html/%s.%s.html\">",
							getenv("BBWEBHOSTURL"), 
							h->hostname, 
							e->column->name);
					}
					else {
						fprintf(fd, "\t<A TARGET=\"_content\" HREF=\"%s%s/bb-hostsvc.sh?HOSTSVC=%s.%s\">",
							getenv("BBWEBHOST"),
							getenv("CGIBINURL"),
							commafy(h->hostname), 
							e->column->name);
					}
					fprintf(fd, "%s (%s)</A>\n",
						h->hostname, e->column->name);
					fprintf(fd, "      </LI>\n");
				}
			}
		}
	}

	if (!anyshown) {
		fprintf(fd, "      <LI>\n");
		fprintf(fd, "        <A TARGET=\"_content\" HREF=\"%s\">No Critical Alerts</A>\n",
			getenv("BBWEBHOSTURL"));
		fprintf(fd, "      </LI>\n");
	}

	fprintf(fd, "    </UL>\n");
	fprintf(fd, "  </BODY>\n"),
	fprintf(fd, "</HTML>\n");

	fclose(fd);
	if (rename(tmpfn, destfn) != 0) {
		errprintf("Cannot move file %s to destination %s\n", tmpfn, destfn);
	}

	return;
}

