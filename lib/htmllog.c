/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for generating HTML version of a status log.          */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: htmllog.c,v 1.23 2005-03-22 09:16:49 henrik Exp $";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "libbbgen.h"
#include "version.h"

#include "htmllog.h"

static char *cgibinurl = NULL;
static char *colfont = NULL;
static char *ackfont = NULL;
static char *rowfont = NULL;

enum histbutton_t histlocation = HIST_BOTTOM;

static void hostsvc_setup(void)
{
	static int setup_done = 0;

	if (setup_done) return;

	getenv_default("NONHISTS", "info,larrd,trends,graphs", NULL);
	getenv_default("BBREL", "bbgen", NULL);
	getenv_default("BBRELDATE", VERSION, NULL);
	getenv_default("CGIBINURL", "/cgi-bin", &cgibinurl);
	getenv_default("MKBBACKFONT", "COLOR=\"#33ebf4\" SIZE=-1\"", &ackfont);
	getenv_default("MKBBCOLFONT", "COLOR=\"#87a9e5\" SIZE=-1\"", &colfont);
	getenv_default("MKBBROWFONT", "SIZE=+1 COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\"", &rowfont);
	getenv_default("BBWEB", "/bb", NULL);
	{
		char *dbuf = malloc(strlen(xgetenv("BBWEB")) + 6);
		sprintf(dbuf, "%s/gifs", xgetenv("BBWEB"));
		getenv_default("BBSKIN", dbuf, NULL);
		xfree(dbuf);
	}

	setup_done = 1;
}


static void historybutton(char *cgibinurl, char *hostname, char *service, char *ip, char *displayname, FILE *output) 
{
	char *tmp1;
	char *tmp2 = (char *)malloc(strlen(service)+3);

	getenv_default("NONHISTS", "info,larrd,trends", NULL);
	tmp1 =  (char *)malloc(strlen(xgetenv("NONHISTS"))+3);

	sprintf(tmp1, ",%s,", xgetenv("NONHISTS"));
	sprintf(tmp2, ",%s,", service);
	if (strstr(tmp1, tmp2) == NULL) {
		fprintf(output, "<BR><BR><CENTER><FORM ACTION=\"%s/bb-hist.sh\"> \
			<INPUT TYPE=SUBMIT VALUE=\"HISTORY\"> \
			<INPUT TYPE=HIDDEN NAME=\"HISTFILE\" VALUE=\"%s.%s\"> \
			<INPUT TYPE=HIDDEN NAME=\"ENTRIES\" VALUE=\"50\"> \
			<INPUT TYPE=HIDDEN NAME=\"IP\" VALUE=\"%s\"> \
			<INPUT TYPE=HIDDEN NAME=\"DISPLAYNAME\" VALUE=\"%s\"> \
			</FORM></CENTER>\n",
			cgibinurl, hostname, service, ip, displayname);
	}

	xfree(tmp2);
	xfree(tmp1);
}


void generate_html_log(char *hostname, char *displayname, char *service, char *ip, 
		       int color, char *sender, char *flags, 
		       time_t logtime, char *timesincechange, 
		       char *firstline, char *restofmsg, 
		       time_t acktime, char *ackmsg, 
		       time_t disabletime, char *dismsg,
		       int is_history, int wantserviceid, int htmlfmt, int hobbitd,
		       char *multigraphs,
		       FILE *output)
{
	int linecount = 0;
	char *p, *multikey;
	larrdrrd_t *larrd = NULL;
	larrdgraph_t *graph = NULL;

	hostsvc_setup();
	if (multigraphs == NULL) multigraphs = ",disk,";

	/* 
	 * Some reports (disk) use the number of lines as a rough measure for how many
	 * graphs to build.
	 * What we *really* should do was to scan the RRD directory and count how many
	 * RRD database files are present matching this service - but that is way too
	 * much overhead for something that might be called on every status logged.
	 */
	multikey = (char *)malloc(strlen(service) + 3);
	sprintf(multikey, ",%s,", service);
	if (strstr(multigraphs, multikey)) {
		/* Count how many lines are in the status message. This is needed by LARRD later */
		linecount = 0; p = restofmsg;
		do {
			/* First skip all whitespace and blank lines */
			while ((*p) && (isspace((int)*p) || iscntrl((int)*p))) p++;
			if (*p) {
				/* We found something that is not blank, so one more line */
				linecount++;
				/* Then skip forward to the EOLN */
				p = strchr(p, '\n');
			}
		} while (p && (*p));
	}
	xfree(multikey);

	sethostenv(displayname, ip, service, colorname(color));
	if (logtime) sethostenv_snapshot(logtime);

	headfoot(output, (is_history ? "histlog" : "hostsvc"), "", "header", color);

	fprintf(output, "<br><br><a name=\"begindata\">&nbsp;</a>\n");

	if (!is_history && (histlocation == HIST_TOP)) historybutton(cgibinurl, hostname, service, ip, displayname, output);

	fprintf(output, "<CENTER><TABLE ALIGN=CENTER BORDER=0 SUMMARY=\"Detail Status\">\n");
	if (wantserviceid) fprintf(output, "<TR><TH><FONT %s>%s - %s</FONT><BR><HR WIDTH=\"60%%\"></TH></TR>\n", rowfont, displayname, service);

	if (disabletime > 0) {
		fprintf(output, "<TR><TD><H3>Disabled until %s</H3></TD></TR>\n", ctime(&disabletime));
		fprintf(output, "<TR><TD><PRE>%s</PRE></TD></TR>\n", dismsg);
		fprintf(output, "<TR><TD><BR><HR>Current status message follows:<HR><BR></TD></TR>\n");

		fprintf(output, "<TR><TD>");
		if (strlen(firstline)) fprintf(output, "<H3>%s</H3>", firstline);	/* Drop the color */
		fprintf(output, "\n");
			
	}
	else {
		char *txt = skipword(firstline);

		fprintf(output, "<TR><TD>");
		if (strlen(txt)) fprintf(output, "<H3>%s</H3>", txt);	/* Drop the color */
		fprintf(output, "\n");
	}

	if (!htmlfmt) fprintf(output, "<PRE>\n");

	do {
		int color;

		p = strchr(restofmsg, '&');
		if (p) {
			*p = '\0';
			fprintf(output, "%s", restofmsg);

			color = parse_color(p+1);
			if (color == -1) {
				fprintf(output, "&");
				restofmsg = p+1;
			}
			else {
				fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0>",
                                                        xgetenv("BBSKIN"), dotgiffilename(color, 0, 0),
							colorname(color),
                                                        xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));

				restofmsg = p+1+strlen(colorname(color));
			}
		}
		else {
			fprintf(output, "%s", restofmsg);
			restofmsg = NULL;
		}
	} while (restofmsg);

	if (!htmlfmt) fprintf(output, "\n</PRE>\n");
	fprintf(output, "</TD></TR></TABLE>\n");

	fprintf(output, "<br><br>\n");
	fprintf(output, "<table align=\"center\" border=0 summary=\"Status report info\">\n");
	fprintf(output, "<tr><td align=\"center\"><font %s>", colfont);
	if (strlen(timesincechange)) fprintf(output, "Status unchanged in %s<br>\n", timesincechange);
	if (sender) fprintf(output, "Status message received from %s<br>\n", sender);
	if (ackmsg) {
		char *ackedby;
		char ackuntil[200];

		MEMDEFINE(ackuntil);

		strftime(ackuntil, sizeof(ackuntil)-1, xgetenv("ACKUNTILMSG"), localtime(&acktime));
		ackuntil[sizeof(ackuntil)-1] = '\0';

		ackedby = strstr(ackmsg, "\nAcked by:");
		if (ackedby) {
			*ackedby = '\0';
			fprintf(output, "<font %s>Current acknowledgment: %s<br>%s<br>%s</font><br>\n", 
				ackfont, ackmsg, (ackedby+1), ackuntil);
			*ackedby = '\n';
		}
		else {
			fprintf(output, "<font %s>Current acknowledgment: %s<br>%s</font><br>\n", 
				ackfont, ackmsg, ackuntil);
		}

		MEMUNDEFINE(ackuntil);
	}
		
	fprintf(output, "</font></td></tr>\n");
	fprintf(output, "</table>\n");

	/* larrd stuff here */
	if (!is_history) {
		larrd = find_larrd_rrd(service, flags);
		if (larrd) {
			graph = find_larrd_graph(larrd->larrdrrdname);
			if (graph == NULL) {
				errprintf("Setup error: Service %s has a graph %s, but no graph-definition\n",
					  service, larrd->larrdrrdname);
			}
		}
	}
	if (larrd && graph) {
		fprintf(output, "<!-- linecount=%d -->\n", linecount);
		fprintf(output, "%s\n", larrd_graph_data(hostname, displayname, service, graph, linecount, 1, hobbitd, 0));
	}

	if (!is_history && (histlocation == HIST_BOTTOM)) historybutton(cgibinurl, hostname, service, ip, displayname, output);

	fprintf(output,"</CENTER>\n");
	headfoot(output, (is_history ? "histlog" : "hostsvc"), "", "footer", color);
}

