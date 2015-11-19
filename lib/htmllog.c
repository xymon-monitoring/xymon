/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for generating HTML version of a status log.          */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libxymon.h"
#include "version.h"

#include "htmllog.h"

static char *cgibinurl = NULL;
static char *colfont = NULL;
static char *ackfont = NULL;
static char *rowfont = NULL;
static char *documentationurl = NULL;
static char *doctarget = NULL;

#define HOSTPOPUP_COMMENT 1
#define HOSTPOPUP_DESCR   2
#define HOSTPOPUP_IP      4

static int hostpopup = (HOSTPOPUP_COMMENT | HOSTPOPUP_DESCR | HOSTPOPUP_IP);

enum histbutton_t histlocation = HIST_BOTTOM;

static void hostpopup_setup(void)
{
	static int setup_done = 0;
	char *val, *p;

	if (setup_done) return;

	val = xgetenv("HOSTPOPUP");
	if (val) {
		/* Clear the setting, since there is an explicit value for it */
		hostpopup = 0;

		for (p = val; (*p); p++) {
			switch (*p) {
			  case 'C': case 'c': hostpopup = (hostpopup | HOSTPOPUP_COMMENT); break;
			  case 'D': case 'd': hostpopup = (hostpopup | HOSTPOPUP_DESCR); break;
			  case 'I': case 'i': hostpopup = (hostpopup | HOSTPOPUP_IP); break;
			  default: break;
			}
		}
	}

	setup_done = 1;
}

static void hostsvc_setup(void)
{
	static int setup_done = 0;

	if (setup_done) return;

	hostpopup_setup();
	getenv_default("NONHISTS", "info,trends,graphs", NULL);
	getenv_default("CGIBINURL", "/cgi-bin", &cgibinurl);
	getenv_default("XYMONPAGEACKFONT", "COLOR=\"#33ebf4\" SIZE=-1\"", &ackfont);
	getenv_default("XYMONPAGECOLFONT", "COLOR=\"#87a9e5\" SIZE=-1\"", &colfont);
	getenv_default("XYMONPAGEROWFONT", "SIZE=+1 COLOR=\"#FFFFCC\" FACE=\"Tahoma, Arial, Helvetica\"", &rowfont);
	getenv_default("XYMONWEB", "/xymon", NULL);
	{
		char *dbuf = malloc(strlen(xgetenv("XYMONWEB")) + 6);
		sprintf(dbuf, "%s/gifs", xgetenv("XYMONWEB"));
		getenv_default("XYMONSKIN", dbuf, NULL);
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
		fprintf(output, "<BR><BR><CENTER><FORM ACTION=\"%s/history.sh\">", cgibinurl);
		fprintf(output, "<INPUT TYPE=SUBMIT VALUE=\"%s\">", htmlquoted(btntxt));
		fprintf(output, "<INPUT TYPE=HIDDEN NAME=\"HISTFILE\" VALUE=\"%s", htmlquoted(hostname));
		fprintf(output, ".%s\">", htmlquoted(service));
		fprintf(output, "<INPUT TYPE=HIDDEN NAME=\"ENTRIES\" VALUE=\"50\">");
		fprintf(output, "<INPUT TYPE=HIDDEN NAME=\"IP\" VALUE=\"%s\">", htmlquoted(ip));
		fprintf(output, "<INPUT TYPE=HIDDEN NAME=\"DISPLAYNAME\" VALUE=\"%s\">", htmlquoted(displayname));
		fprintf(output, "</FORM></CENTER>\n");
	}

	xfree(tmp2);
	xfree(tmp1);
}

static void textwithcolorimg(char *msg, FILE *output)
{
	char *p, *restofmsg;

	restofmsg = msg;
	do {
		int color, acked, recent;

		color = -1; acked = recent = 0;
		p = strchr(restofmsg, '&');
		if (p) {
			*p = '\0';
			fprintf(output, "%s", restofmsg);
			*p = '&';

			if (strncmp(p, "&red", 4) == 0) color = COL_RED;
			else if (strncmp(p, "&yellow", 7) == 0) color = COL_YELLOW;
			else if (strncmp(p, "&green", 6) == 0) color = COL_GREEN;
			else if (strncmp(p, "&clear", 6) == 0) color = COL_CLEAR;
			else if (strncmp(p, "&blue", 5) == 0) color = COL_BLUE;
			else if (strncmp(p, "&purple", 7) == 0) color = COL_PURPLE;

			if (color == -1) {
				fprintf(output, "&");
				restofmsg = p+1;
			}
			else {
				acked = (strncmp(p + 1 + strlen(colorname(color)), "-acked", 6) == 0);
				recent = (strncmp(p + 1 + strlen(colorname(color)), "-recent", 7) == 0);

				fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0>",
                                                        xgetenv("XYMONSKIN"), dotgiffilename(color, acked, !recent),
							colorname(color),
                                                        xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));

				restofmsg = p+1+strlen(colorname(color));
				if (acked) restofmsg += 6;
				if (recent) restofmsg += 7;
			}
		}
		else {
			fprintf(output, "%s", restofmsg);
			restofmsg = NULL;
		}
	} while (restofmsg);
}


void generate_html_log(char *hostname, char *displayname, char *service, char *ip, 
		       int color, int flapping, char *sender, char *flags, 
		       time_t logtime, char *timesincechange, 
		       char *firstline, char *restofmsg, char *modifiers,
		       time_t acktime, char *ackmsg, char *acklist,
		       time_t disabletime, char *dismsg,
		       int is_history, int wantserviceid, int htmlfmt, int locatorbased,
		       char *multigraphs,
		       char *linktoclient,
		       char *prio, char *ttgroup, char *ttextra,
		       int graphtime,
		       FILE *output)
{
	int linecount = 0;
	xymonrrd_t *rrd = NULL;
	xymongraph_t *graph = NULL;
	char *tplfile = "hostsvc";
	char *graphs;
	char *graphsenv;
	char *graphsptr;
	time_t now = getcurrenttime(NULL);

	if (graphtime == 0) {
		if (getenv("TRENDSECONDS")) graphtime = atoi(getenv("TRENDSECONDS"));
		else graphtime = 48*60*60;
	}

	hostsvc_setup();
	if (!displayname) displayname = hostname;
	sethostenv(displayname, ip, service, colorname(color), hostname);
	if (logtime) sethostenv_snapshot(logtime);

	if (is_history) tplfile = "histlog";
	if (strcmp(service, xgetenv("INFOCOLUMN")) == 0) tplfile = "info";
	headfoot(output, tplfile, "", "header", color);

	if (strcmp(service, xgetenv("TRENDSCOLUMN")) == 0) {
		int formfile;
		char formfn[PATH_MAX];

		sprintf(formfn, "%s/web/trends_form", xgetenv("XYMONHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;
			int n;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1); *inbuf = '\0';
			n = read(formfile, inbuf, st.st_size);
			if (n > 0) inbuf[n] = '\0';
			close(formfile);

			sethostenv_backsecs(graphtime);
			output_parsed(output, inbuf, color, 0);
			xfree(inbuf);
		}
	}

	if (prio) {
		int formfile;
		char formfn[PATH_MAX];

		sprintf(formfn, "%s/web/critack_form", xgetenv("XYMONHOME"));
		formfile = open(formfn, O_RDONLY);

		if (formfile >= 0) {
			char *inbuf;
			struct stat st;
			int n;

			fstat(formfile, &st);
			inbuf = (char *) malloc(st.st_size + 1); *inbuf = '\0';
			n = read(formfile, inbuf, st.st_size);
			if (n > 0) inbuf[st.st_size] = '\0';
			close(formfile);

			sethostenv_critack(atoi(prio), ttgroup, ttextra, 
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
				fprintf(output, "<td><font %s>%s</font></td>", ackfont, htmlquoted(ackedby));
				fprintf(output, "<td><font %s>%s&nbsp;-&nbsp;%s</font></td>", ackfont, receivedstr, untilstr);
				fprintf(output, "<td><font %s>%s</font></td>", ackfont, htmlquoted(msg));
				fprintf(output, "</tr>\n");
			}

			if (eol) { *eol = '\n'; bol = eol+1; } else bol = NULL;
		} while (bol);

		fprintf(output, "</table>\n");
	}

	fprintf(output, "<br><br><a name=\"begindata\">&nbsp;</a>\n");

	if (flapping) fprintf(output, "<CENTER><B>WARNING: Flapping status</B></CENTER>\n");

	if (histlocation == HIST_TOP) {
		historybutton(cgibinurl, hostname, service, ip, displayname,
			      (is_history ? "Full History" : "HISTORY"), output);
	}

	fprintf(output, "<CENTER><TABLE ALIGN=CENTER BORDER=0 SUMMARY=\"Detail Status\">\n");

	if (wantserviceid) {
		fprintf(output, "<TR><TH><FONT %s>", rowfont);
		fprintf(output, "%s - ", htmlquoted(displayname));
		fprintf(output, "%s", htmlquoted(service));
		fprintf(output, "</FONT><BR><HR WIDTH=\"60%%\"></TH></TR>\n");
	}

	if (disabletime != 0) {
		fprintf(output, "<TR><TD ALIGN=LEFT><H3>Disabled until %s</H3></TD></TR>\n", 
			(disabletime == -1 ? "OK" : ctime(&disabletime)));
		fprintf(output, "<TR><TD ALIGN=LEFT><PRE>%s</PRE></TD></TR>\n", htmlquoted(dismsg));
		fprintf(output, "<TR><TD ALIGN=LEFT><BR><HR>Current status message follows:<HR><BR></TD></TR>\n");

		fprintf(output, "<TR><TD ALIGN=LEFT>");
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
			fprintf(output, "<TR><TD ALIGN=LEFT><H3>Planned downtime: %s</H3></TD></TR>\n", htmlquoted(dismsg));
			fprintf(output, "<TR><TD ALIGN=LEFT><BR><HR>Current status message follows:<HR><BR></TD></TR>\n");
		}

		if (modifiers) {
			char *modtxt;

			nldecode(modifiers);
			fprintf(output, "<TR><TD ALIGN=LEFT>");
			modtxt = strtok(modifiers, "\n");
			while (modtxt) {
				fprintf(output, "<H3>");
				textwithcolorimg(modtxt, output);
				fprintf(output, "</H3>");
				modtxt = strtok(NULL, "\n");
				if (modtxt) fprintf(output, "<br>");
			}
			fprintf(output, "\n");
		}

		fprintf(output, "<TR><TD ALIGN=LEFT>");
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
				ackfont, htmlquoted(ackmsg), (ackedby+1), ackuntil);
			*ackedby = '\n';
		}
		else {
			fprintf(output, "<font %s>Current acknowledgment: %s<br>%s</font><br>\n", 
				ackfont, htmlquoted(ackmsg), ackuntil);
		}

		MEMUNDEFINE(ackuntil);
	}

	fprintf(output, "</font></td></tr>\n");
	fprintf(output, "</table>\n");

	/* trends stuff here */
	if (!is_history) {
		if (! (flags && (strchr(flags, 'R') != NULL)) ) rrd = find_xymon_rrd(service, flags);
		if (rrd) {
			graph = find_xymon_graph(rrd->xymonrrdname);
			if (graph == NULL) {
				errprintf("Setup error: Service %s has a graph %s, but no graph-definition\n",
					  service, rrd->xymonrrdname);
			}
		}
	}
	if (rrd && graph) {
		int may_have_rrd = 1;

		/*
		 * See if there is already a linecount in the report.
		 * If there is, this overrides the calculation here.
		 *
		 * From Francesco Duranti's hobbit-perl-client.
		 */
		char *lcstr = strstr(restofmsg, "<!-- linecount=");
		if (lcstr) {
			linecount=atoi(lcstr+15);
		}
		else {
			char *p, *multikey;

			/* quotas, snapshot and TblSpace are generated by hobbit-perl-client */
			if (multigraphs == NULL) multigraphs = ",disk,inode,qtree,quotas,snapshot,TblSpace,if_load,";

			/* Not all devmon statuses have graphs, so try to avoid generating graph links unless there is one */
			if (strncmp(rrd->xymonrrdname,"devmon",6) == 0) may_have_rrd=0;

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

				/* FD: The "TblSpace" report from dbcheck.pl need to take out a total of 3 line of heade */
				int tblspacereport = (strstr(restofmsg, "dbcheck.pl") != NULL);

				/* Old BB clients do not send in df's header line */
				int header = (strchr(firstline, '/') == NULL);

				/* Count how many lines are in the status message. This is needed by xymond_graph later */
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

						if (strlen(p) > 10 &&  *p == '<' ) {
							/* Check if this is a devmon RRD header, reset the linecount to -2, as we will see a DS line and a Devmon banner*/
							if(!strncmp(p, "<!--DEVMON",10)) {
								linecount = -2;
								may_have_rrd=1;
							}
						}

						/* Then skip forward to the EOLN */
						p = strchr(p, '\n');
					}
				} while (p && (*p));

				/* Do not count the 'df' header line */
				if (!netwarediskreport && header && (linecount > 1)) linecount--;
				if (tblspacereport && (linecount > 2)) linecount-=2;

			}
			xfree(multikey);
		}

		if (may_have_rrd) {
			fprintf(output, "<!-- linecount=%d -->\n", linecount);
			fprintf(output, "<a name=\"begingraph\">&nbsp;</a>\n");

			/* Get the GRAPHS_* environment setting */
			graphs = (char *)malloc(7 + strlen(service) + 1);
			sprintf(graphs, "GRAPHS_%s", service);
			graphsenv=getenv(graphs);
			if (graphsenv) {
				fprintf(output, "<!-- GRAPHS_%s: %s -->\n", service, graphsenv);
				/* check for strtokens */
				graphsptr = strtok(graphsenv,",");
				while (graphsptr != NULL) {
					// fprintf(output, "<!-- found: %s -->\n", graphsptr);
					graph->xymonrrdname = strdup(graphsptr);
					fprintf(output, "%s\n", xymon_graph_data(hostname, displayname, graphsptr, color, graph, linecount, HG_WITHOUT_STALE_RRDS, locatorbased, now-graphtime, now));
					// next token
					graphsptr = strtok(NULL,",");
				}

			}
			else {
				fprintf(output, "%s\n", xymon_graph_data(hostname, displayname, service, color, graph, linecount, HG_WITHOUT_STALE_RRDS, locatorbased, now-graphtime, now));
			}
			xfree(graphs);
		}
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


static char *nameandcomment(void *host, char *hostname, int usetooltip)
{
	static char *result = NULL;
	char *cmt, *disp, *hname;

	if (result) xfree(result);

	hostpopup_setup();

	/* For summary "hosts", we have no hinfo record. */
	if (!host) return hostname;

	hname = xmh_item(host, XMH_HOSTNAME);
	disp = xmh_item(host, XMH_DISPLAYNAME);

	cmt = NULL;
	if (!cmt && (hostpopup & HOSTPOPUP_COMMENT))             cmt = xmh_item(host, XMH_COMMENT); 
	if (!cmt && usetooltip && (hostpopup & HOSTPOPUP_DESCR)) cmt = xmh_item(host, XMH_DESCRIPTION);
	if (!cmt && usetooltip && (hostpopup & HOSTPOPUP_IP))    cmt = xmh_item(host, XMH_IP);

	if (disp == NULL) disp = hname;

	if (cmt) {
		if (usetooltip) {
			/* Thanks to Marco Schoemaker for suggesting the use of <span title...> */
			result = (char *)malloc(strlen(disp) + strlen(cmt) + 30);
			sprintf(result, "<span title=\"%s\">%s</span>", cmt, disp);
		}
		else {
			result = (char *)malloc(strlen(disp) + strlen(cmt) + 4);
			sprintf(result, "%s (%s)", disp, cmt);
		}
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

char *hostnamehtml(char *hostname, char *defaultlink, int usetooltip)
{
	static char result[4096];
	void *hinfo = hostinfo(hostname);
	char *hostlinkurl;

	if (!doctarget) doctarget = strdup("");

	/* First the hostname and a notes-link.
	 *
	 * If a documentation CGI is defined, use that.
	 *
	 * else if a host has a direct notes-link, use that.
	 *
	 * else if no direct link and we are doing a nongreen/critical page, 
	 * provide a link to the main page with this host (there
	 * may be links to documentation in some page-title).
	 *
	 * else just put the hostname there.
	 */
	if (documentationurl) {
		snprintf(result, sizeof(result), "<A HREF=\"%s\" %s><FONT %s>%s</FONT></A>",
			urldoclink(documentationurl, hostname),
			doctarget, xgetenv("XYMONPAGEROWFONT"), nameandcomment(hinfo, hostname, usetooltip));
	}
	else if ((hostlinkurl = hostlink(hostname)) != NULL) {
		snprintf(result, sizeof(result), "<A HREF=\"%s\" %s><FONT %s>%s</FONT></A>",
			hostlinkurl, doctarget, xgetenv("XYMONPAGEROWFONT"), nameandcomment(hinfo, hostname, usetooltip));
	}
	else if (defaultlink) {
		/* Provide a link to the page where this host lives */
		snprintf(result, sizeof(result), "<A HREF=\"%s/%s\" %s><FONT %s>%s</FONT></A>",
			xgetenv("XYMONWEB"), defaultlink, doctarget,
			xgetenv("XYMONPAGEROWFONT"), nameandcomment(hinfo, hostname, usetooltip));
	}
	else {
		snprintf(result, sizeof(result), "<FONT %s>%s</FONT>",
			xgetenv("XYMONPAGEROWFONT"), nameandcomment(hinfo, hostname, usetooltip));
	}

	return result;
}

