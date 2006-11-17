/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for generating HTML version of a status log.          */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: htmllog.c,v 1.53 2006-11-17 14:50:01 henrik Exp $";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libbbgen.h"
#include "version.h"

#include "htmllog.h"

static char *cgibinurl = NULL;
static char *colfont = NULL;
static char *ackfont = NULL;
static char *rowfont = NULL;
static char *documentationurl = NULL;
static char *doctarget = NULL;

enum histbutton_t histlocation = HIST_BOTTOM;

static void hostsvc_setup(void)
{
	static int setup_done = 0;

	if (setup_done) return;

	getenv_default("NONHISTS", "info,trends,graphs", NULL);
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


static void historybutton(char *cgibinurl, char *hostname, char *service, char *ip, char *displayname, char *btntxt, FILE *output) 
{
	char *tmp1;
	char *tmp2 = (char *)malloc(strlen(service)+3);

	getenv_default("NONHISTS", "info,trends", NULL);
	tmp1 =  (char *)malloc(strlen(xgetenv("NONHISTS"))+3);

	sprintf(tmp1, ",%s,", xgetenv("NONHISTS"));
	sprintf(tmp2, ",%s,", service);
	if (strstr(tmp1, tmp2) == NULL) {
		fprintf(output, "<BR><BR><CENTER><FORM ACTION=\"%s/bb-hist.sh\"> \
			<INPUT TYPE=SUBMIT VALUE=\"%s\"> \
			<INPUT TYPE=HIDDEN NAME=\"HISTFILE\" VALUE=\"%s.%s\"> \
			<INPUT TYPE=HIDDEN NAME=\"ENTRIES\" VALUE=\"50\"> \
			<INPUT TYPE=HIDDEN NAME=\"IP\" VALUE=\"%s\"> \
			<INPUT TYPE=HIDDEN NAME=\"DISPLAYNAME\" VALUE=\"%s\"> \
			</FORM></CENTER>\n",
			cgibinurl, btntxt, hostname, service, ip, displayname);
	}

	xfree(tmp2);
	xfree(tmp1);
}

static void textwithcolorimg(char *msg, FILE *output)
{
	char *p, *restofmsg;

	restofmsg = msg;
	do {
		int color;

		p = strchr(restofmsg, '&');
		if (p) {
			*p = '\0';
			fprintf(output, "%s", restofmsg);
			*p = '&';

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
}


void generate_html_log(char *hostname, char *displayname, char *service, char *ip, 
		       int color, char *sender, char *flags, 
		       time_t logtime, char *timesincechange, 
		       char *firstline, char *restofmsg, 
		       time_t acktime, char *ackmsg, char *acklist,
		       time_t disabletime, char *dismsg,
		       int is_history, int wantserviceid, int htmlfmt, int locatorbased,
		       char *multigraphs,
		       char *linktoclient,
		       char *nkprio, char *nkttgroup, char *nkttextra,
		       FILE *output)
{
	int linecount = 0;
	hobbitrrd_t *rrd = NULL;
	hobbitgraph_t *graph = NULL;
	char *tplfile = "hostsvc";

	hostsvc_setup();
	if (!displayname) displayname = hostname;
	sethostenv(displayname, ip, service, colorname(color), hostname);
	if (logtime) sethostenv_snapshot(logtime);

	if (is_history) tplfile = "histlog";
	if (strcmp(service, xgetenv("INFOCOLUMN")) == 0) tplfile = "info";
	headfoot(output, tplfile, "", "header", color);

	if (nkprio) {
		int formfile;
		char formfn[PATH_MAX];

		sprintf(formfn, "%s/web/nkack_form", xgetenv("BBHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1);
			read(formfile, inbuf, st.st_size);
			inbuf[st.st_size] = '\0';
			close(formfile);

			sethostenv_nkack(atoi(nkprio), nkttgroup, nkttextra, 
				 hostsvcurl(hostname, xgetenv("INFOCOLUMN"), 1), hostlink(hostname));

			output_parsed(output, inbuf, color, 0);
			xfree(inbuf);
		}
	}

	if (acklist && *acklist) {
		/* received:validuntil:level:ackedby:msg */
		time_t received, validuntil;
		int level; 
		char *ackedby, *msg;
		char *bol, *eol, *tok;
		char receivedstr[200];
		char untilstr[200];

		fprintf(output, "<table border=0 summary=\"Ack info\" align=center>\n");
		fprintf(output, "<tr>");
		fprintf(output, "<th align=center colspan=4><font %s>Acknowledgments</font></th>", ackfont);
		fprintf(output, "</tr>\n");
		fprintf(output, "<tr>");
		fprintf(output, "<th align=left><font %s>Level</font></th>", ackfont);
		fprintf(output, "<th align=left><font %s>From</font></th>", ackfont);
		fprintf(output, "<th align=left><font %s>Validity</font></th>", ackfont);
		fprintf(output, "<th align=left><font %s>Message</font></th>", ackfont);
		fprintf(output, "</tr>\n");

		nldecode(acklist);

		bol = acklist;
		do {
			eol = strchr(bol, '\n'); if (eol) *eol = '\0';

			tok = strtok(bol, ":");
			if (tok) { received = atoi(tok); tok = strtok(NULL, ":"); } else received = 0;
			if (tok) { validuntil = atoi(tok); tok = strtok(NULL, ":"); } else validuntil = 0;
			if (tok) { level = atoi(tok); tok = strtok(NULL, ":"); } else level = -1;
			if (tok) { ackedby = tok; tok = strtok(NULL, "\n"); } else ackedby = NULL;
			if (tok) msg = tok; else msg = NULL;

			if (received && validuntil && (level >= 0) && ackedby && msg) {
				strftime(receivedstr, sizeof(receivedstr)-1, "%Y-%m-%d %H:%M", localtime(&received));
				strftime(untilstr, sizeof(untilstr)-1, "%Y-%m-%d %H:%M", localtime(&validuntil));
				fprintf(output, "<tr>");
				fprintf(output, "<td align=center><font %s>%d</font></td>", ackfont, level);
				fprintf(output, "<td><font %s>%s</font></td>", ackfont, ackedby);
				fprintf(output, "<td><font %s>%s&nbsp;-&nbsp;%s</font></td>", ackfont, receivedstr, untilstr);
				fprintf(output, "<td><font %s>%s</font></td>", ackfont, msg);
				fprintf(output, "</tr>\n");
			}

			if (eol) { *eol = '\n'; bol = eol+1; } else bol = NULL;
		} while (bol);

		fprintf(output, "</table>\n");
	}

	fprintf(output, "<br><br><a name=\"begindata\">&nbsp;</a>\n");

	if (histlocation == HIST_TOP) {
		historybutton(cgibinurl, hostname, service, ip, displayname,
			      (is_history ? "Full History" : "HISTORY"), output);
	}

	fprintf(output, "<CENTER><TABLE ALIGN=CENTER BORDER=0 SUMMARY=\"Detail Status\">\n");
	if (wantserviceid) fprintf(output, "<TR><TH><FONT %s>%s - %s</FONT><BR><HR WIDTH=\"60%%\"></TH></TR>\n", rowfont, displayname, service);

	if (disabletime != 0) {
		fprintf(output, "<TR><TD><H3>Disabled until %s</H3></TD></TR>\n", 
			(disabletime == -1 ? "OK" : ctime(&disabletime)));
		fprintf(output, "<TR><TD><PRE>%s</PRE></TD></TR>\n", dismsg);
		fprintf(output, "<TR><TD><BR><HR>Current status message follows:<HR><BR></TD></TR>\n");

		fprintf(output, "<TR><TD>");
		if (strlen(firstline)) {
			fprintf(output, "<H3>");
			textwithcolorimg(firstline, output);
			fprintf(output, "</H3>");	/* Drop the color */
		}
		fprintf(output, "\n");
			
	}
	else {
		char *txt = skipword(firstline);

		if (dismsg) {
			fprintf(output, "<TR><TD><H3>Planned downtime: %s</H3></TD></TR>\n", dismsg);
			fprintf(output, "<TR><TD><BR><HR>Current status message follows:<HR><BR></TD></TR>\n");
		}

		fprintf(output, "<TR><TD>");
		if (strlen(txt)) {
			fprintf(output, "<H3>");
			textwithcolorimg(txt, output);
			fprintf(output, "</H3>");	/* Drop the color */
		}
		fprintf(output, "\n");
	}

	if (!htmlfmt) fprintf(output, "<PRE>\n");
	textwithcolorimg(restofmsg, output);
	if (!htmlfmt) fprintf(output, "\n</PRE>\n");

	fprintf(output, "</TD></TR></TABLE>\n");

	fprintf(output, "<br><br>\n");
	fprintf(output, "<table align=\"center\" border=0 summary=\"Status report info\">\n");
	fprintf(output, "<tr><td align=\"center\"><font %s>", colfont);
	if (strlen(timesincechange)) fprintf(output, "Status unchanged in %s<br>\n", timesincechange);
	if (sender) fprintf(output, "Status message received from %s<br>\n", sender);
	if (linktoclient) fprintf(output, "<a href=\"%s\">Client data</a> available<br>\n", linktoclient);
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

	/* trends stuff here */
	if (!is_history) {
		rrd = find_hobbit_rrd(service, flags);
		if (rrd) {
			graph = find_hobbit_graph(rrd->hobbitrrdname);
			if (graph == NULL) {
				errprintf("Setup error: Service %s has a graph %s, but no graph-definition\n",
					  service, rrd->hobbitrrdname);
			}
		}
	}
	if (rrd && graph) {
		char *p, *multikey;
		if (multigraphs == NULL) multigraphs = ",disk,inode,qtree,";

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
			/* The "disk" report from the NetWare client puts a "warning light" on all entries */
			int netwarediskreport = (strstr(firstline, "NetWare Volumes") != NULL);

			/* Count how many lines are in the status message. This is needed by hobbitd_graph later */
			linecount = 0; p = restofmsg;
			do {
				/* First skip all whitespace and blank lines */
				while ((*p) && (isspace((int)*p) || iscntrl((int)*p))) p++;
				if (*p) {
					if ((*p == '&') && (parse_color(p+1) != -1)) {
						/* A "warninglight" line - skip it, unless its from a Netware box */
						if (netwarediskreport) linecount++;
					}
					else {
						/* We found something that is not blank, so one more line */
						if (!netwarediskreport) linecount++;
					}
					/* Then skip forward to the EOLN */
					p = strchr(p, '\n');
				}
			} while (p && (*p));

			/* There is probably a header line ... */
			if (!netwarediskreport && (linecount > 1)) linecount--;
		}
		xfree(multikey);

		fprintf(output, "<!-- linecount=%d -->\n", linecount);
		fprintf(output, "%s\n", hobbit_graph_data(hostname, displayname, service, color, graph, linecount, HG_WITHOUT_STALE_RRDS, HG_PLAIN_LINK, locatorbased));
	}

	if (histlocation == HIST_BOTTOM) {
		historybutton(cgibinurl, hostname, service, ip, displayname,
			      (is_history ? "Full History" : "HISTORY"), output);
	}

	fprintf(output,"</CENTER>\n");
	headfoot(output, tplfile, "", "footer", color);
}

char *alttag(char *columnname, int color, int acked, int propagate, char *age)
{
	static char tag[1024];
	size_t remain;

	remain = sizeof(tag) - 1;
	remain -= snprintf(tag, remain, "%s:%s:", columnname, colorname(color));
	if (remain > 20) {
		if (acked) { strncat(tag, "acked:", remain); remain -= 6; }
		if (!propagate) { strncat(tag, "nopropagate:", remain); remain -= 12; }
		strncat(tag, age, remain);
	}
	tag[sizeof(tag)-1] = '\0';

	return tag;
}


static char *nameandcomment(namelist_t *host, char *hostname)
{
	static char *result = NULL;
	char *cmt, *disp, *hname;

	if (result) xfree(result);

	/* For summary "hosts", we have no hinfo record. */
	if (!host) return hostname;

	hname = bbh_item(host, BBH_HOSTNAME);
	disp = bbh_item(host, BBH_DISPLAYNAME);
	cmt = bbh_item(host, BBH_COMMENT);
	if (disp == NULL) disp = hname;

	if (cmt) {
		result = (char *)malloc(strlen(disp) + strlen(cmt) + 4);
		sprintf(result, "%s (%s)", disp, cmt);
		return result;
	}
	else 
		return disp;
}

static char *urldoclink(const char *docurl, const char *hostname)
{
	/*
	 * docurl is a user defined text string to build
	 * a documentation url. It is expanded with the
	 * hostname.
	 */

	static char linkurl[PATH_MAX];

	if (docurl) {
		sprintf(linkurl, docurl, hostname);
	}
	else {
		linkurl[0] = '\0';
	}

	return linkurl;
}


void setdocurl(char *url)
{
	if (documentationurl) xfree(documentationurl);
	documentationurl = strdup(url);
}

void setdoctarget(char *target)
{
	if (doctarget) xfree(doctarget);
	doctarget = strdup(target);
}

char *hostnamehtml(char *hostname, char *defaultlink)
{
	static char result[4096];
	namelist_t *hinfo = hostinfo(hostname);
	char *hostlinkurl;

	if (!doctarget) doctarget = strdup("");

	/* First the hostname and a notes-link.
	 *
	 * If a documentation CGI is defined, use that.
	 *
	 * else if a host has a direct notes-link, use that.
	 *
	 * else if no direct link and we are doing a BB2/BBNK page, 
	 * provide a link to the main page with this host (there
	 * may be links to documentation in some page-title).
	 *
	 * else just put the hostname there.
	 */
	if (documentationurl) {
		snprintf(result, sizeof(result), "<A HREF=\"%s\" %s><FONT %s>%s</FONT></A>",
			urldoclink(documentationurl, hostname),
			doctarget, xgetenv("MKBBROWFONT"), nameandcomment(hinfo, hostname));
	}
	else if ((hostlinkurl = hostlink(hostname)) != NULL) {
		snprintf(result, sizeof(result), "<A HREF=\"%s\" %s><FONT %s>%s</FONT></A>",
			hostlinkurl, doctarget, xgetenv("MKBBROWFONT"), nameandcomment(hinfo, hostname));
	}
	else if (defaultlink) {
		/* Provide a link to the page where this host lives */
		snprintf(result, sizeof(result), "<A HREF=\"%s/%s\" %s><FONT %s>%s</FONT></A>",
			xgetenv("BBWEB"), defaultlink, doctarget,
			xgetenv("MKBBROWFONT"), nameandcomment(hinfo, hostname));
	}
	else {
		snprintf(result, sizeof(result), "<FONT %s>%s</FONT>",
			xgetenv("MKBBROWFONT"), nameandcomment(hinfo, hostname));
	}

	return result;
}

