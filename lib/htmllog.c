/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for generating HTML version of a status log.          */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: htmllog.c,v 1.3 2004-11-06 10:02:20 henrik Exp $";

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
	getenv_default("MKBBCOLFONT", "COLOR=teal SIZE=-1\"", &colfont);
	getenv_default("MKBBROWFONT", "SIZE=+1 COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\"", &rowfont);
	getenv_default("BBWEB", "/bb", NULL);
	{
		char *dbuf = malloc(strlen(getenv("BBWEB")) + 6);
		sprintf(dbuf, "%s/gifs", getenv("BBWEB"));
		getenv_default("BBSKIN", dbuf, NULL);
		free(dbuf);
	}

	setup_done = 1;
}


static void historybutton(char *cgibinurl, char *hostname, char *service, char *ip, FILE *output) 
{
	char *tmp1;
	char *tmp2 = (char *)malloc(strlen(service)+3);

	getenv_default("NONHISTS", "info,larrd,trends", NULL);
	tmp1 =  (char *)malloc(strlen(getenv("NONHISTS"))+3);

	sprintf(tmp1, ",%s,", getenv("NONHISTS"));
	sprintf(tmp2, ",%s,", service);
	if (strstr(tmp1, tmp2) == NULL) {
		fprintf(output, "<BR><BR><CENTER><FORM ACTION=\"%s/bb-hist.sh\"> \
			<INPUT TYPE=SUBMIT VALUE=\"HISTORY\"> \
			<INPUT TYPE=HIDDEN NAME=\"HISTFILE\" VALUE=\"%s.%s\"> \
			<INPUT TYPE=HIDDEN NAME=\"ENTRIES\" VALUE=\"50\"> \
			<INPUT TYPE=HIDDEN NAME=\"IP\" VALUE=\"%s\"> \
			</FORM></CENTER>\n",
			cgibinurl, hostname, service, ip);
	}

	free(tmp2);
	free(tmp1);
}


void generate_html_log(char *hostname, char *displayname, char *service, char *ip, 
		       int color, char *sender, char *flags, 
		       time_t logtime, char *timesincechange, 
		       char *firstline, char *restofmsg, char *ackmsg, 
		       int is_history, FILE *output)
{
	int linecount;
	char *p;
	larrdsvc_t *larrd = NULL;

	hostsvc_setup();

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

	sethostenv(displayname, ip, service, colorname(color));
	if (logtime) sethostenv_snapshot(logtime);

	headfoot(output, (is_history ? "histlog" : "hostsvc"), "", "header", color);

	fprintf(output, "<br><br><a name=\"begindata\">&nbsp;</a>\n");

	if (!is_history && (histlocation == HIST_TOP)) historybutton(cgibinurl, hostname, service, ip, output);

	fprintf(output, "<CENTER><TABLE ALIGN=CENTER BORDER=0>\n");
	fprintf(output, "<TR><TH><FONT %s>%s - %s</FONT><BR><HR WIDTH=\"60%%\"></TH></TR>\n", rowfont, displayname, service);
	fprintf(output, "<TR><TD><H3>%s</H3>\n", skipword(firstline));	/* Drop the color */
	fprintf(output, "<PRE>\n");

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
                                                        getenv("BBSKIN"), dotgiffilename(color, 0, 0),
							colorname(color),
                                                        getenv("DOTHEIGHT"), getenv("DOTWIDTH"));

				restofmsg = p+1+strlen(colorname(color));
			}
		}
		else {
			fprintf(output, "%s", restofmsg);
			restofmsg = NULL;
		}
	} while (restofmsg);

	fprintf(output, "\n</PRE>\n");
	fprintf(output, "</TD></TR></TABLE>\n");

	fprintf(output, "<br><br>\n");
	fprintf(output, "<table align=\"center\" border=0>\n");
	fprintf(output, "<tr><td align=\"center\"><font %s>", colfont);
	if (strlen(timesincechange)) fprintf(output, "Status unchanged in %s<br>\n", timesincechange);
	if (sender) fprintf(output, "Status message received from %s<br>\n", sender);
	if (ackmsg) fprintf(output, "Current acknowledgment: %s<br>\n", ackmsg);
	fprintf(output, "</font></td></tr>\n");
	fprintf(output, "</table>\n");

	/* larrd stuff here */
	if (!is_history) larrd = find_larrd(service, flags);
	if (larrd) {
		/* 
		 * If this service uses part-names (currently, only disk does),
		 * then setup a link for each of the part graphs.
		 */
		if (larrd->larrdpartname) {
			int start;

			fprintf(output, "<!-- linecount=%d -->\n", linecount);
			for (start=0; (start < linecount); start += 6) {
				fprintf(output,"<BR><BR><CENTER><A HREF=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&%s=%d..%d&amp;disp=%s\"><IMG SRC=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;%s=%d..%d&amp;graph=hourly&ampdisp=%s\" ALT=\"&nbsp;\" BORDER=0></A><BR></CENTER>\n",
					cgibinurl, hostname, larrd->larrdsvcname, larrd->larrdpartname,
					start, (((start+5) < linecount) ? start+5 : linecount-1), displayname,
					cgibinurl, hostname, larrd->larrdsvcname, larrd->larrdpartname,
					start, (((start+5) < linecount) ? start+5 : linecount-1), displayname);
			}
		}
		else {
				fprintf(output,"<BR><BR><CENTER><A HREF=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;disp=%s\"><IMG SRC=\"%s/larrd-grapher.cgi?host=%s&amp;service=%s&amp;disp=%s&amp;graph=hourly\"ALT=\"&nbsp;\" BORDER=0></A><BR></CENTER>\n",
					cgibinurl, hostname, larrd->larrdsvcname, displayname,
					cgibinurl, hostname, larrd->larrdsvcname, displayname);
		}
	}

	if (!is_history && (histlocation == HIST_BOTTOM)) historybutton(cgibinurl, hostname, service, ip, output);
	
	headfoot(output, (is_history ? "histlog" : "hostsvc"), "", "footer", color);
}

