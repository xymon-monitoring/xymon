/*----------------------------------------------------------------------------*/
/* Xymon webpage generator tool.                                              */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libxymon.h"

enum { S_NAME, S_SENDER, S_TIME } sorttype = S_NAME;
char *sortstring = "name";
int maxage = 300;
enum { O_HTML, O_TXT } outform = O_HTML;

void parse_query(void)
{
	cgidata_t *cgidata, *cwalk;

	cgidata = cgi_request();

	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwalk->value points to the value (may be an empty string).
		 */

		if (strcmp(cwalk->name, "SORT") == 0) {
			sortstring = strdup(cwalk->value);

			if (strcmp(cwalk->value, "name") == 0) sorttype = S_NAME;
			else if (strcmp(cwalk->value, "sender") == 0) sorttype = S_SENDER;
			else if (strcmp(cwalk->value, "time") == 0) sorttype = S_TIME;
		}
		else if (strcmp(cwalk->name, "MAXAGE") == 0) {
			maxage = atoi(cwalk->value);
			if (maxage <= 0) maxage = 300;
		}
		else if (strcmp(cwalk->name, "TEXT") == 0) {
			outform = O_TXT;
		}

		cwalk = cwalk->next;
	}
}

typedef struct ghost_t {
	char *sender;
	unsigned long senderval;
	char *name;
	time_t tstamp;
	void *candidate;
} ghost_t;


int hostname_compare(const void *v1, const void *v2)
{
	ghost_t *r1 = (ghost_t *)v1;
	ghost_t *r2 = (ghost_t *)v2;

	return strcasecmp(r1->name, r2->name);
}

int sender_compare(const void *v1, const void *v2)
{
	ghost_t *r1 = (ghost_t *)v1;
	ghost_t *r2 = (ghost_t *)v2;

	if (r1->senderval < r2->senderval) return -1;
	else if (r1->senderval > r2->senderval) return 1;
	else return 0;
}

int time_compare(const void *v1, const void *v2)
{
	ghost_t *r1 = (ghost_t *)v1;
	ghost_t *r2 = (ghost_t *)v2;

	if (r1->tstamp > r2->tstamp) return -1;
	else if (r1->tstamp < r2->tstamp) return 1;
	else return 0;
}

void find_candidate(ghost_t *rec)
{
	/*
	 * Try to find an existing host definition for this ghost.
	 * We look at the IP-address.
	 * If the ghost reports with a FQDN, look for a host with
	 * the same hostname without domain.
	 * If the ghost reports a plain hostname (no domain), look
	 * for a FQDN record with the same plain hostname.
	 */

	void *hrec;
	int found;
	char *gdelim, *tdelim, *ghostnofqdn, *hname;

	ghostnofqdn = rec->name;
	if ((gdelim = strchr(ghostnofqdn, '.')) != NULL) *gdelim = '\0';

	for (hrec = first_host(), found = 0; (hrec && !found); hrec = next_host(hrec, 0)) {
		/* Check the IP */
		found = (strcmp(rec->sender, xmh_item(hrec, XMH_IP)) == 0);

		if (!found) {
			/* Check if hostnames w/o domain match */
			hname = xmh_item(hrec, XMH_HOSTNAME);
			if ((tdelim = strchr(hname, '.')) != NULL) *tdelim = '\0';
			found = (strcasecmp(ghostnofqdn, hname) == 0);
			if (tdelim) *tdelim = '.';
		}

		if (found) rec->candidate = hrec;
	}

	if (gdelim) *gdelim = '.';
}

int main(int argc, char *argv[])
{
	int argi;
	char *hffile = "ghosts";
	int bgcolor = COL_BLUE;
	char *ghosts = NULL;
	sendreturn_t *sres;

	libxymon_init(argv[0]);
	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--hffile=")) {
			char *p = strchr(argv[argi], '=');
			hffile = strdup(p+1);
		}
		else if (standardoption(argv[argi])) {
			if (showhelp) return 0;
		}
	}

	load_hostnames(xgetenv("HOSTSCFG"), NULL, get_fqdn());
	parse_query();

	switch (outform) {
	  case O_HTML:
		fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
		headfoot(stdout, hffile, "", "header", bgcolor);
		break;
	  case O_TXT:
		fprintf(stdout, "Content-type: text/plain\n\n");
		break;
	}

	sres = newsendreturnbuf(1, NULL);

	if (sendmessage("ghostlist", NULL, XYMON_TIMEOUT, sres) == XYMONSEND_OK) {
		char *bol, *eoln, *name, *sender, *timestr;
		time_t tstamp, now;
		int count, idx;
		ghost_t *ghosttable;

		ghosts = getsendreturnstr(sres, 1);

		/* Count the number of lines */
		for (bol = ghosts, count=0; (bol); bol = strchr(bol, '\n')) {
			if (*bol == '\n') bol++;
			count++;
		}
		ghosttable = (ghost_t *)calloc(count+1, sizeof(ghost_t));

		idx = count = 0;
		tstamp = now = getcurrenttime(NULL);
		bol = ghosts;
		while (bol) {
			name = sender = timestr = NULL;

			eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
			name = strtok(bol, "|");
			if (name) sender = strtok(NULL, "|");
			if (sender) timestr = strtok(NULL, "|");

			if (timestr) tstamp = atol(timestr);

			if (name && sender && timestr && (tstamp > (now - maxage))) {
				int i1, i2, i3, i4;

				sscanf(sender, "%d.%d.%d.%d", &i1, &i2, &i3, &i4);
				ghosttable[idx].sender = sender;
				ghosttable[idx].senderval = (i1 << 24) + (i2 << 16) + (i3 << 8) + i4;
				ghosttable[idx].name = name;
				ghosttable[idx].tstamp = tstamp;
				find_candidate(&ghosttable[idx]);
				idx++; count++;
			}

			if (eoln) eoln++;
			bol = eoln;
		}

		switch (sorttype) {
		  case S_NAME:
			qsort(&ghosttable[0], count, sizeof(ghost_t), hostname_compare);
			break;

		  case S_SENDER:
			qsort(&ghosttable[0], count, sizeof(ghost_t), sender_compare);
			break;

		  case S_TIME:
			qsort(&ghosttable[0], count, sizeof(ghost_t), time_compare);
			break;
		}

		if (outform == O_HTML) {
			fprintf(stdout, "<table align=center>\n");
			fprintf(stdout, "<tr>");
			fprintf(stdout, "<th align=left><a href=\"ghostlist.sh?SORT=name&MAXAGE=%d\">Hostname</a></th>", maxage);
			fprintf(stdout, "<th align=left><a href=\"ghostlist.sh?SORT=sender&MAXAGE=%d\">Sent from</a></th>", maxage);
			fprintf(stdout, "<th align=left>Candidate</th>");
			fprintf(stdout, "<th align=right><a href=\"ghostlist.sh?SORT=time&MAXAGE=%d\">Report age</a></th>", maxage);
			fprintf(stdout, "</tr>\n");
		}

		for (idx = 0; (idx < count); idx++) {
			if (!ghosttable[idx].name) continue;
			if (!ghosttable[idx].sender) continue;

			switch (outform) {
			  case O_HTML:
				fprintf(stdout, "<tr><td align=left>%s</td><td align=left>%s</td>",
					ghosttable[idx].name, 
					ghosttable[idx].sender);

				if (ghosttable[idx].candidate) {
					fprintf(stdout, "<td align=left><a href=\"%s\">%s</a></td>",
						hostsvcurl(xmh_item(ghosttable[idx].candidate, XMH_HOSTNAME), xgetenv("INFOCOLUMN"), 1),
						xmh_item(ghosttable[idx].candidate, XMH_HOSTNAME));
				}
				else {
					fprintf(stdout, "<td>&nbsp;</td>");
				}

				fprintf(stdout, "<td align=right>%ld:%02ld</td></tr>\n",
					(now - ghosttable[idx].tstamp)/60, (now - ghosttable[idx].tstamp)%60);
				break;

			  case O_TXT:
				fprintf(stdout, "%s\t\t%s\n", ghosttable[idx].sender, ghosttable[idx].name);
				break;
			}
		}

		if (outform == O_HTML) {
			fprintf(stdout, "</table>\n");
			fprintf(stdout, "<br><br><center><a href=\"ghostlist.sh?SORT=%s&MAXAGE=%d&TEXT\">Text report</a></center>\n", htmlquoted(sortstring), maxage);
		}
	}
	else
		fprintf(stdout, "<h3><center>Failed to retrieve ghostlist from server</center></h3>\n");

	freesendreturnbuf(sres);

	if (outform == O_HTML) {
		headfoot(stdout, hffile, "", "footer", bgcolor);
	}

	return 0;
}

