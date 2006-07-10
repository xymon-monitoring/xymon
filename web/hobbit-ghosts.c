/*----------------------------------------------------------------------------*/
/* Hobbit webpage generator tool.                                             */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-ghosts.c,v 1.3 2006-07-10 15:22:49 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"

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


int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *hffile = "ghosts";
	int bgcolor = COL_BLUE;
	char *ghosts = NULL;

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--hffile=")) {
			char *p = strchr(argv[argi], '=');
			hffile = strdup(p+1);
		}
	}

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

	if (sendmessage("ghostlist", NULL, NULL, &ghosts, 1, BBTALK_TIMEOUT) == BB_OK) {
		char *bol, *eoln, *name, *sender, *timestr;
		time_t tstamp, now;
		int count, idx;
		ghost_t *ghosttable;

		/* Count the number of lines */
		for (bol = ghosts, count=0; (bol); bol = strchr(bol, '\n')) {
			if (*bol == '\n') bol++;
			count++;
		}
		ghosttable = (ghost_t *)calloc(count+1, sizeof(ghost_t));

		idx = count = 0;
		tstamp = now = time(NULL);
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
			fprintf(stdout, "<th align=left><a href=\"hobbit-ghosts.sh?SORT=name&MAXAGE=%d\">Hostname</a></th>", maxage);
			fprintf(stdout, "<th align=left><a href=\"hobbit-ghosts.sh?SORT=sender&MAXAGE=%d\">Sent from</a></th>", maxage);
			fprintf(stdout, "<th align=right><a href=\"hobbit-ghosts.sh?SORT=time&MAXAGE=%d\">Report age</a></th>", maxage);
			fprintf(stdout, "</tr>\n");
		}

		for (idx = 0; (idx < count); idx++) {
			if (!ghosttable[idx].name) continue;
			if (!ghosttable[idx].sender) continue;

			switch (outform) {
			  case O_HTML:
				fprintf(stdout, "<tr><td align=left>%s</td><td align=left>%s</td><td align=right>%ld:%02ld</td></tr>\n",
					ghosttable[idx].name, 
					ghosttable[idx].sender, 
					(now - ghosttable[idx].tstamp)/60, (now - ghosttable[idx].tstamp)%60);
				break;

			  case O_TXT:
				fprintf(stdout, "%s\t\t%s\n", ghosttable[idx].sender, ghosttable[idx].name);
				break;
			}
		}

		if (outform == O_HTML) {
			fprintf(stdout, "</table>\n");
			fprintf(stdout, "<br><br><center><a href=\"hobbit-ghosts.sh?SORT=%s&MAXAGE=%d&TEXT\">Text report</a></center>\n", sortstring, maxage);
		}
	}
	else
		fprintf(stdout, "<h3><center>Failed to retrieve ghostlist from server</center></h3>\n");

	if (outform == O_HTML) {
		headfoot(stdout, hffile, "", "footer", bgcolor);
	}

	return 0;
}

