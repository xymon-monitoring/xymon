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
/* Copyright (C) 2002-2003 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: pagegen.c,v 1.96 2003-09-08 21:41:50 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "bbgen.h"
#include "util.h"
#include "debug.h"
#include "loaddata.h"
#include "pagegen.h"
#include "larrdgen.h"
#include "infogen.h"
#include "eventlog.h"
#include "acklog.h"
#include "bb-replog.h"
#include "reportdata.h"

int  subpagecolumns = 1;
int  hostsbeforepages = 0;
char *includecolumns = NULL;
int  sort_grouponly_items = 0; /* Standard BB behaviour: Dont sort group-only items */
char *documentationcgi = NULL;
char *htmlextension = ".html"; /* Filename extension for generated files */
char *doctargetspec = " TARGET=\"_blank\"";
char *defaultpagetitle = NULL;
int  pagetitlelinks = 0;
int  maxrowsbeforeheading = 0;
int  bb2eventlog = 1;
int  bb2acklog = 1;

/* Format strings for htaccess files */
char *htaccess = NULL;
char *bbhtaccess = NULL;
char *bbpagehtaccess = NULL;
char *bbsubpagehtaccess = NULL;

char *hf_prefix[3];            /* header/footer prefixes for BB, BB2, BBNK pages*/


void select_headers_and_footers(char *prefix)
{
	hf_prefix[PAGE_BB]  = (char *) malloc(strlen(prefix)+1); sprintf(hf_prefix[PAGE_BB],  "%s",   prefix);
	hf_prefix[PAGE_BB2] = (char *) malloc(strlen(prefix)+2); sprintf(hf_prefix[PAGE_BB2], "%s2",  prefix);
	hf_prefix[PAGE_NK]  = (char *) malloc(strlen(prefix)+3); sprintf(hf_prefix[PAGE_NK],  "%snk", prefix);
}


int interesting_column(int pagetype, int color, int alert, char *columnname, char *onlycols)
{
	/*
	 * Decides if a given column is to be included on a page.
	 */

	if (pagetype == PAGE_BB) {
		/* Fast-path the BB page. */

		int result = 1;

		if (onlycols) {
			/* onlycols explicitly list the columns to include (for bb page only) */
			char *search;

			/* loaddata::init_group guarantees that onlycols start and end with a '|' */
			search = (char *) malloc(strlen(columnname)+3);
			sprintf(search, "|%s|", columnname);
			result = (strstr(onlycols, search) != NULL);
			free(search);
		}

		/* This is final. */
		return result;
	}

	/* pagetype is now known NOT to be PAGE_BB */

	/* LARRD and INFO columns are always included on non-BB pages */
	if (larrdcol && (strcmp(columnname, larrdcol) == 0)) return 1;
	if (infocol && (strcmp(columnname, infocol) == 0)) return 1;

	if (includecolumns) {
		/* includecolumns are other columns to include always on non-BB pages (bb2, bbnk) */
		char *col1 = (char *) malloc(strlen(columnname)+3); /* 3 = 2 commas and a NULL */
		int result;

		sprintf(col1, ",%s,", columnname);
		result = (strstr(includecolumns, col1) != NULL);
		free(col1);

		/* If included, done here. Otherwise may be included further down. */
		if (result) return result;
	}

	switch (pagetype) {
	  case PAGE_BB2:
		  /* Include all non-green tests */
		  return ((color == COL_RED) || (color == COL_YELLOW) || (color == COL_PURPLE));

	  case PAGE_NK:
		  /* Include only RED or YELLOW tests with "alert" property set. 
		   * Even then, the "conn" test is included only when RED.
		   */
		if (alert) {
			if (color == COL_RED)  return 1;
			if ( (color == COL_YELLOW) || (color == COL_CLEAR) ) return (strcmp(columnname, "conn") != 0);
		}
		break;
	}

	return 0;
}

col_list_t *gen_column_list(host_t *hostlist, int pagetype, char *onlycols)
{
	/*
	 * Build a list of all the columns that are in use by
	 * any host in the hostlist passed as parameter.
	 * The column list will be sorted by column name, except 
	 * when doing a "group-only" and the standard BB behaviour.
	 */

	col_list_t	*head;
	host_t		*h;
	entry_t		*e;
	col_list_t	*newlistitem, *collist_walk;

	/* Code de-obfuscation trick: Add a null record as the head item */
	/* Simplifies handling since head != NULL and we never have to insert at head of list */
	head = (col_list_t *) malloc(sizeof(col_list_t));
	head->column = &null_column;
	head->next = NULL;

	if (!sort_grouponly_items && (onlycols != NULL)) {

		/* 
		 * This is the original BB handling of "group-only". 
		 * All items are included, whether there are any test data
		 * for a column or not. The order given in the group-only
		 * directive is maintained.
		 * We simple convert the group-only directive to a
		 * col_list_t linked list.
		 */

		char *p1 = onlycols;
		char *p2;
		bbgen_col_t *col;

		collist_walk = head;
		do {
			if (*p1 == '|') p1++;

			p2 = strchr(p1, '|');
			if (p2) {
				*p2 = '\0';

				for (col = colhead; (col && (strcmp(p1, col->name) != 0)); col = col->next);
				if (col) {
					newlistitem = (col_list_t *) malloc(sizeof(col_list_t));
					newlistitem->column = col;
					newlistitem->next = NULL;
					collist_walk->next = newlistitem;
					collist_walk = collist_walk->next;
				}
				*p2 = '|';
			}

			p1 = p2;
		} while (p1 != NULL);

		/* Skip the dummy record */
		collist_walk = head; head = head->next; free(collist_walk);

		/* We're done - dont even look at the actual test data. */
		return (head);
	}

	for (h = hostlist; (h); h = h->next) {
		/*
		 * This is for everything except "standard group-only" handled above.
		 * So also for group-only with --sort-group-only-items.
		 * Note that in a group-only here, items may be left out if there
		 * are no test data for a column at all.
		 */

		for (e = h->entries; (e); e = e->next) {
			if (interesting_column(pagetype, e->color, e->alert, e->column->name, onlycols)) {
				/* See where e->column should go in list */
				collist_walk = head; 
				while ( (collist_walk->next && 
                               		strcmp(e->column->name, ((col_list_t *)(collist_walk->next))->column->name) > 0) ) {
					collist_walk = collist_walk->next;
				}

				if ((collist_walk->next == NULL) || ((col_list_t *)(collist_walk->next))->column != e->column) {
					/* collist_walk points to the entry before the new one */
					newlistitem = (col_list_t *) malloc(sizeof(col_list_t));
					newlistitem->column = e->column;
					newlistitem->next = collist_walk->next;
					collist_walk->next = newlistitem;
				}
			}
		}
	}

	/* Skip the dummy record */
	collist_walk = head; head = head->next; free(collist_walk);
	return (head);
}


void setup_htaccess(const char *pagepath)
{
	char htaccessfn[MAX_PATH];
	char htaccesscontent[1024];

	if (htaccess == NULL) return;

	htaccesscontent[0] = '\0';

	if (strlen(pagepath) == 0) {
		sprintf(htaccessfn, "%s", htaccess);
		if (bbhtaccess) strcpy(htaccesscontent, bbhtaccess);
	}
	else {
		char *pagename, *subpagename, *p;
		char *path = malcop(pagepath);

		for (p = path + strlen(path) - 1; ((p > path) && (*p == '/')); p--) *p = '\0';

		sprintf(htaccessfn, "%s/%s", path, htaccess);

		pagename = path; if (*pagename == '/') pagename++;
		p = strchr(pagename, '/'); 
		if (p) { 
			*p = '\0'; 
			subpagename = p+1;
			p = strchr(subpagename, '/');
			if (p) *p = '\0';
			if (bbsubpagehtaccess) sprintf(htaccesscontent, bbsubpagehtaccess, pagename, subpagename);
		}
		else {
			if (bbpagehtaccess) sprintf(htaccesscontent, bbpagehtaccess, pagename);
		}

		free(path);
	}

	if (strlen(htaccesscontent)) {
		FILE *fd;
		struct stat st;

		if (stat(htaccessfn, &st) == 0) {
			dprintf("htaccess file %s exists, not overwritten\n", htaccessfn);
			return;
		}

		fd = fopen(htaccessfn, "w");
		if (fd) {
			fprintf(fd, "%s\n", htaccesscontent);
			fclose(fd);
		}
		else {
			errprintf("Cannot create %s\n", htaccessfn);
		}
	}
}


static char *nameandcomment(host_t *host)
{
	static char result[1024];

	if (host->comment) {
		sprintf(result, "%s (%s)", host->displayname, host->comment);
	}
	else strcpy(result, host->displayname);

	return result;
}

void do_hosts(host_t *head, char *onlycols, FILE *output, char *grouptitle, int pagetype, char *pagepath)
{
	/*
	 * This routine outputs the host part of a page or a group.
	 * I.e. it generates buttons and links to all the tests for
	 * a host, and the host docs.
	 */

	host_t	*h;
	entry_t	*e;
	col_list_t *groupcols, *gc;
	int	genstatic;
	int	columncount;
	char	*bbskin;
	int	maxbanksize = 0;
	int	anyplainhosts = 0;
	int	rowcount = 0;

	if (head == NULL)
		return;

	bbskin = malcop(getenv("BBSKIN"));

	/* Generate static or dynamic links (from BBLOGSTATUS) ? */
	genstatic = generate_static();

	fprintf(output, "<A NAME=hosts-blk>&nbsp;</A>\n\n");

	for (h = head; (h); h = h->next) {
		if (h->banksize > maxbanksize) maxbanksize = h->banksize;
		if (h->banksize == 0) anyplainhosts = 1;
	}

	if (maxbanksize == 0) {
		/* No modembanks - normal hostlist with columns and stuff */
		groupcols = gen_column_list(head, pagetype, onlycols);
		for (columncount=0, gc=groupcols; (gc); gc = gc->next, columncount++) ;
	}
	else {
		/* There are modembanks here! */
		if (anyplainhosts) {
			errprintf("WARNING: Modembank displays should be in their own group or page.\nMixing normal hosts with modembanks yield strange output.\n");
		}
		groupcols = NULL;
		columncount = maxbanksize;
	}

	if (groupcols || (maxbanksize > 0)) {

		int width;

		width = atoi(getenv("DOTWIDTH"));
		if ((width < 0) || (width > 50)) width = 16;
		width += 4;

		/* Start the table ... */
		fprintf(output, "<CENTER><TABLE SUMMARY=\"Group Block\" BORDER=0 CELLPADDING=2>\n");

		/* Generate the host rows */
		for (h = head; (h); h = h->next) {
			/* If there is a host pretitle, show it. */
			dprintf("Host:%s, pretitle:%s\n", h->hostname, textornull(h->pretitle));

			if (h->pretitle && (rowcount == 0)) {
				fprintf(output, "<tr><td colspan=%d align=center valign=middle><br><font %s>%s</font></td></tr>\n", 
						columncount+1, getenv("MKBBTITLE"), h->pretitle);
			}

			if (h->pretitle || (rowcount == 0)) {
				/* output group title and column headings */
				fprintf(output, "<TR><TD VALIGN=MIDDLE ROWSPAN=2><CENTER><FONT %s>%s</FONT></CENTER></TD>\n", 
					getenv("MKBBTITLE"), grouptitle);
				if ((groupcols == NULL) && (maxbanksize > 0)) {
					int i,j;

					fprintf(output, "<TD><TABLE BORDER=0>\n");
					for (i=0; (i < maxbanksize); i+=16) {
						fprintf(output, "<TR>\n");
						for (j=i; (((j-i) < 16) && (j < maxbanksize)); j++) {
							fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=%d>", width);
							fprintf(output, " <FONT %s><B>%d</B></FONT>", 
								getenv("MKBBCOLFONT"), j);
							fprintf(output, " </TD>\n");
						}
						fprintf(output, "</TR>\n");
					}
					fprintf(output, "</TABLE></TD>\n");
				}
				else {
					for (gc=groupcols; (gc); gc = gc->next) {
						fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
						fprintf(output, " <A HREF=\"%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n", 
							columnlink(gc->column->link, gc->column->name), 
							getenv("MKBBCOLFONT"), gc->column->name);
					}
				}
				fprintf(output, "</TR> \n<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n\n", columncount);
			}

			fprintf(output, "<TR>\n <TD NOWRAP><A NAME=\"%s\">&nbsp;</A>\n", h->hostname);
			if (maxrowsbeforeheading) rowcount = (rowcount + 1) % maxrowsbeforeheading;
			else rowcount++;

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
			if (documentationcgi) {
				fprintf(output, "<A HREF=\"%s/%s\" %s><FONT %s>%s</FONT></A>\n </TD>",
					getenv("CGIBINURL"), cgidoclink(documentationcgi, h->hostname),
					doctargetspec, getenv("MKBBROWFONT"), nameandcomment(h));
			}
			else if (h->link != &null_link) {
				fprintf(output, "<A HREF=\"%s\" %s><FONT %s>%s</FONT></A>\n </TD>",
					hostlink(h->link), doctargetspec, getenv("MKBBROWFONT"), nameandcomment(h));
			}
			else if (pagetype != PAGE_BB) {
				/* Provide a link to the page where this host lives */
				fprintf(output, "<A HREF=\"%s/%s\" %s><FONT %s>%s</FONT></A>\n </TD>",
					getenv("BBWEB"), hostpage_link(h), doctargetspec,
					getenv("MKBBROWFONT"), nameandcomment(h));
			}
			else {
				fprintf(output, "<FONT %s>%s</FONT>\n </TD>",
					getenv("MKBBROWFONT"), nameandcomment(h));
			}

			/* Then the columns. */
			if ((groupcols == NULL) && (h->banksize > 0)) {
				int i, j;
				char alttag[30];
				unsigned int baseip, ip1, ip2, ip3, ip4;

				sscanf(h->ip, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);
				baseip = IPtou32(ip1, ip2, ip3, ip4);

				fprintf(output, "<TD ALIGN=CENTER><TABLE BORDER=0>");
				for (i=0; (i < h->banksize); i+=16) {
					fprintf(output, "<TR>\n");
					for (j=i; (((j-i) < 16) && (j < h->banksize)); j++) {
						fprintf(output, "<TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=%d>", width);

						if (genstatic) {
							/*
							 * Dont use htmlextension here - it's for the
							 * pages generated by bbd.
							 */
							fprintf(output, "<A HREF=\"%s/html/dialup.%s.html\">",
								getenv("BBWEB"), h->hostname);
						}
						else {
							fprintf(output, "<A HREF=\"%s/bb-hostsvc.sh?HOSTSVC=dialup.%s\">",
								getenv("CGIBINURL"), commafy(h->hostname));
						}
	
						sprintf(alttag, "%s:%s", u32toIP(baseip+j), colorname(h->banks[j]));
						fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
							bbskin, dotgiffilename(h->banks[j], 0, 0),
							alttag, getenv("DOTHEIGHT"), getenv("DOTWIDTH"));

						fprintf(output, "</TD>\n");
					}
					fprintf(output, "</TR>\n");
				}
				fprintf(output, "</TABLE></TD>\n");
			}
			for (gc = groupcols; (gc); gc = gc->next) {
				fprintf(output, "<TD ALIGN=CENTER>");

				/* Any column entry for this host ? */
				for (e = h->entries; (e && (e->column != gc->column)); e = e->next) ;
				if (e == NULL) {
					fprintf(output, "-");
				}
				else if (e->histlogname) {
					/* Snapshot points to historical logfile */
					fprintf(output, "<A HREF=\"%s/bb-histlog.sh?HOST=%s&amp;SERVICE=%s&amp;TIMEBUF=%s\">", 
						getenv("CGIBINURL"), h->hostname, e->column->name, e->histlogname);

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						bbskin, dotgiffilename(e->color, e->acked, e->oldage),
						alttag(e),
						getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
				}
				else if (reportstart == 0) {
					/* Standard webpage */
					char *skin;

					skin = (e->skin ? e->skin : bbskin);

					if (e->sumurl) {
						/* A summary host. */
						fprintf(output, "<A HREF=\"%s\">", e->sumurl);
					}
					else if (genstatic) {
						/*
						 * Dont use htmlextension here - it's for the
						 * pages generated by bbd.
						 */
						fprintf(output, "<A HREF=\"%s/html/%s.%s.html\">",
							getenv("BBWEB"), h->hostname, e->column->name);
					}
					else {
						fprintf(output, "<A HREF=\"%s/bb-hostsvc.sh?HOSTSVC=%s.%s\">",
							getenv("CGIBINURL"), commafy(h->hostname), e->column->name);
					}

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						skin, dotgiffilename(e->color, e->acked, e->oldage),
						alttag(e),
						getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
				}
				else {
					/* Report format output */
					if ((e->color == COL_GREEN) || (e->color == COL_CLEAR)) {
						fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0>",
							bbskin, dotgiffilename(e->color, e->acked, e->oldage),
							colorname(e->color),
							getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
					}
					else {
						if (dynamicreport) {
							fprintf(output, "<A HREF=\"%s/bb-replog.sh?HOSTSVC=%s.%s&amp;IP=%s",
								getenv("CGIBINURL"), commafy(h->hostname), e->column->name, h->ip);
							fprintf(output, "&amp;COLOR=%s&amp;PCT=%.2f&amp;ST=%u&amp;END=%u",
								colorname(e->color), e->repinfo->fullavailability, 
								(unsigned int)e->repinfo->reportstart, (unsigned int)reportend);
							fprintf(output, "&amp;RED=%.2f&amp;YEL=%.2f&amp;GRE=%.2f&amp;PUR=%.2f&amp;CLE=%.2f&amp;BLU=%.2f",
								e->repinfo->fullpct[COL_RED], e->repinfo->fullpct[COL_YELLOW], 
								e->repinfo->fullpct[COL_GREEN], e->repinfo->fullpct[COL_PURPLE], 
								e->repinfo->fullpct[COL_CLEAR], e->repinfo->fullpct[COL_BLUE]);
							fprintf(output, "&amp;STYLE=%s&amp;FSTATE=%s",
								stylenames[reportstyle], e->repinfo->fstate);
							fprintf(output, "&amp;REDCNT=%d&amp;YELCNT=%d&amp;GRECNT=%d&amp;PURCNT=%d&amp;CLECNT=%d&amp;BLUCNT=%d",
								e->repinfo->count[COL_RED], e->repinfo->count[COL_YELLOW], 
								e->repinfo->count[COL_GREEN], e->repinfo->count[COL_PURPLE], 
								e->repinfo->count[COL_CLEAR], e->repinfo->count[COL_BLUE]);
							if (h->reporttime) fprintf(output, "&amp;REPORTTIME=%s", h->reporttime);
							fprintf(output, "&amp;WARNPCT=%.2f", h->reportwarnlevel);
							fprintf(output, "&amp;RECENTGIFS=%d", use_recentgifs);
							fprintf(output, "\">\n");
						}
						else {
							FILE *htmlrep, *textrep;
							char htmlrepfn[MAX_PATH];
							char textrepfn[MAX_PATH];
							char textrepurl[MAX_PATH];

							/* File names are relative - current directory is the output dir */
							/* pagepath is either empty, or it ends with a '/' */
							sprintf(htmlrepfn, "%s%s-%s%s", 
								pagepath, h->hostname, e->column->name, htmlextension);
							sprintf(textrepfn, "%savail-%s-%s.txt",
								pagepath, h->hostname, e->column->name);
							sprintf(textrepurl, "%s/%s", 
								getenv("BBWEB"), textrepfn);

							htmlrep = fopen(htmlrepfn, "w");
							textrep = fopen(textrepfn, "w");

							/* Pre-build the test-specific report */
							restore_replogs(e->causes);
							generate_replog(htmlrep, textrep, textrepurl,
									h->hostname, h->ip, e->column->name, e->color, 
									reportstyle, reportstart, reportend,
									reportwarnlevel, reportgreenlevel, e->repinfo);

							if (textrep) fclose(textrep);
							if (htmlrep) fclose(htmlrep);

							fprintf(output, "<A HREF=\"%s-%s%s\">\n", 
								h->hostname, e->column->name, htmlextension);
						}
						fprintf(output, "<FONT SIZE=-1 COLOR=%s><B>%.2f</B></FONT></A>\n",
							colorname(e->color), e->repinfo->fullavailability);
					}
				}
				fprintf(output, "</TD>\n");
			}

			fprintf(output, "</TR>\n\n");
		}

		fprintf(output, "</TABLE></CENTER><BR><BR>\n");
	}

	/* Free the columnlist allocated by gen_column_list() */
	while (groupcols) {
		gc = groupcols;
		groupcols = groupcols->next;
		free(gc);
	}

	free(bbskin);
}

void do_groups(group_t *head, FILE *output, char *pagepath)
{
	/*
	 * This routine generates all the groups on a given page.
	 * It also triggers generating host output for hosts
	 * within the groups.
	 */

	group_t *g;

	if (head == NULL)
		return;

	fprintf(output, "<CENTER> \n\n<A NAME=begindata>&nbsp;</A>\n");

	for (g = head; (g); g = g->next) {
		if (g->hosts && g->pretitle) {
			fprintf(output, "<CENTER><TABLE BORDER=0>\n");
			fprintf(output, "  <TR><TD><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", getenv("MKBBTITLE"), g->pretitle);
			fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
			fprintf(output, "</TABLE></CENTER>\n");
		}

		do_hosts(g->hosts, g->onlycols, output, g->title, PAGE_BB, pagepath);
	}
	fprintf(output, "\n</CENTER>\n");
}

void do_summaries(dispsummary_t *sums, FILE *output)
{
	/*
	 * Generates output for summary statuses received from others.
	 */

	dispsummary_t *s;
	host_t *sumhosts = NULL;
	host_t *walk;

	if (sums == NULL) {
		/* No summary items */
		return;
	}

	for (s=sums; (s); s = s->next) {
		/* Generate host records out of all unique s->row values */

		host_t *newhost;
		entry_t *newentry;
		dispsummary_t *s2;

		/* Do we already have it ? */
		for (newhost = sumhosts; (newhost && (strcmp(s->row, newhost->hostname) != 0) ); newhost = newhost->next);

		if (newhost == NULL) {
			/* New summary "host" */
			newhost = init_host(s->row, NULL, NULL, NULL, 0,0,0,0, 0, 0, 0.0, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL, 0);

			/*
			 * Cannot have the pseudo host in the official hostlist,
			 * it messes up the WML generator later.
			 */
			if (hosthead->hostentry == newhost) hosthead = hosthead->next;

			/* Insert into sorted host list */
			if ((!sumhosts) || (strcmp(newhost->hostname, sumhosts->hostname) < 0)) {
				/* Empty list, or new entry goes before list head item */
				newhost->next = sumhosts;
				sumhosts = newhost;
			}
			else {
				/* Walk list until we find element that goes after new item */
				for (walk = sumhosts; 
			      	(walk->next && (strcmp(newhost->hostname, ((host_t *)walk->next)->hostname) > 0)); 
			      	walk = walk->next) ;

				/* "walk" points to element before the new item */
				newhost->next = walk->next;
				walk->next = newhost;
			}


			/* Setup the "event" records from the column records */
			for (s2 = sums; (s2); s2 = s2->next) {
				
				if (strcmp(s2->row, s->row) == 0) {
					newentry = (entry_t *) malloc(sizeof(entry_t));

					newentry->column = find_or_create_column(s2->column);
					newentry->color = s2->color;
					strcpy(newentry->age, "");
					newentry->oldage = 1; /* Use standard gifs */
					newentry->acked = 0;
					newentry->alert = 0;
					newentry->onwap = 0;
					newentry->propagate = 1;
					newentry->sumurl = s2->url;
					newentry->skin = NULL;
					newentry->testflags = NULL;
					newentry->repinfo = NULL;
					newentry->causes = NULL;
					newentry->histlogname = NULL;
					newentry->next = newhost->entries;
					newhost->entries = newentry;
				}
			}
		}
	}

	fprintf(output, "<A NAME=\"summaries-blk\">&nbsp;</A>\n");
	fprintf(output, "<CENTER>\n");
	fprintf(output, "<TABLE SUMMARY=\"Summary Block\" BORDER=0><TR><TD>\n");
	fprintf(output, "<CENTER><FONT %s>\n", getenv("MKBBTITLE"));
	fprintf(output, "%s\n", getenv("MKBBREMOTE"));
	fprintf(output, "</FONT></CENTER></TD></TR><TR><TD>\n");
	fprintf(output, "<HR WIDTH=\"100%%\"></TD></TR>\n");
	fprintf(output, "<TR><TD>\n");

	do_hosts(sumhosts, NULL, output, "", 0, NULL);

	fprintf(output, "</TD></TR></TABLE>\n");
	fprintf(output, "</CENTER>\n");
}

void do_page_subpages(FILE *output, bbgen_page_t *subs, char *pagepath)
{
	/*
	 * This routine does NOT generate subpages!
	 * Instead, it generates the LINKS to the subpages below any given page.
	 */

	bbgen_page_t	*p;
	link_t  *link;
	int	currentcolumn;
	char	pagelink[MAX_PATH];

	if (subs) {
		fprintf(output, "<A NAME=\"pages-blk\">&nbsp;</A>\n");

		fprintf(output, "<BR>\n<CENTER>\n");
		fprintf(output, "<TABLE SUMMARY=\"Page Block\" BORDER=0>\n");

		currentcolumn = 0;
		for (p = subs; (p); p = p->next) {
			if (p->pretitle) {
				/*
				 * Output a page-link title text.
				 */
				if (currentcolumn != 0) {
					fprintf(output, "</TR>\n");
					currentcolumn = 0;
				}

				fprintf(output, "<TR><TD COLSPAN=%d><CENTER> \n<FONT %s>\n", 
						(2*subpagecolumns + (subpagecolumns - 1)), getenv("MKBBTITLE"));
				fprintf(output, "   <br>%s\n", p->pretitle);
				fprintf(output, "</FONT></CENTER></TD></TR>\n");
				fprintf(output, "<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n", 
						(2*subpagecolumns + (subpagecolumns - 1)));
			}

			if (currentcolumn == 0) fprintf(output, "<TR>\n");

			link = find_link(p->name);
			sprintf(pagelink, "%s/%s/%s/%s%s", getenv("BBWEB"), pagepath, p->name, p->name, htmlextension);

			fprintf(output, "<TD><FONT %s>", getenv("MKBBROWFONT"));
			if (link != &null_link) {
				fprintf(output, "<A HREF=\"%s\">%s</A>", hostlink(link), p->title);
			}
			else if (pagetitlelinks) {
				fprintf(output, "<A HREF=\"%s\">%s</A>", cleanurl(pagelink), p->title);
			}
			else {
				fprintf(output, "%s", p->title);
			}
			fprintf(output, "</FONT></TD>\n");

			fprintf(output, "<TD><CENTER><A HREF=\"%s\">", cleanurl(pagelink));
			fprintf(output, "<IMG SRC=\"%s/%s\" WIDTH=\"%s\" HEIGHT=\"%s\" BORDER=0 ALT=\"%s\"></A>", 
				getenv("BBSKIN"), dotgiffilename(p->color, 0, p->oldage), 
				getenv("DOTWIDTH"), getenv("DOTHEIGHT"),
				colorname(p->color));
			fprintf(output, "</CENTER></TD>\n");

			if (currentcolumn == (subpagecolumns-1)) {
				fprintf(output, "</TR>\n");
				currentcolumn = 0;
			}
			else {
				/* Need to have a little space between columns */
				fprintf(output, "<TD WIDTH=\"%s\">&nbsp;</TD>", getenv("DOTWIDTH"));
				currentcolumn++;
			}
		}

		if (currentcolumn != 0) fprintf(output, "</TR>\n");

		fprintf(output, "</TABLE><BR><BR>\n");
		fprintf(output, "</CENTER>\n");
	}
}


void do_one_page(bbgen_page_t *page, dispsummary_t *sums, int embedded)
{
	FILE	*output;
	char	pagepath[MAX_PATH];
	char	filename[MAX_PATH];
	char	tmpfilename[MAX_PATH];
	char	*dirdelim;

	pagepath[0] = '\0';
	if (embedded) {
		output = stdout;
	}
	else {
		if (page->parent == NULL) {
			char	indexfilename[MAX_PATH];

			/* top level page */
			sprintf(filename, "bb%s", htmlextension);
			sprintf(indexfilename, "index%s", htmlextension);
			symlink(filename, indexfilename);
			dprintf("Symlinking %s -> %s\n", filename, indexfilename);
		}
		else {
			char tmppath[MAX_PATH];
			bbgen_page_t *pgwalk;
	
			for (pgwalk = page; (pgwalk); pgwalk = pgwalk->parent) {
				if (strlen(pgwalk->name)) {
					sprintf(tmppath, "%s/%s/", pgwalk->name, pagepath);
					strcpy(pagepath, tmppath);
				}
			}
	
			sprintf(filename, "%s/%s%s", pagepath, page->name, htmlextension);
		}
		sprintf(tmpfilename, "%s.tmp", filename);


		/* Try creating the output file. If it fails, we may need to create the directories */
		output = fopen(tmpfilename, "w");
		if (output == NULL) {
			char indexfilename[MAX_PATH];
			char pagebasename[MAX_PATH];
			char *p;
			int res;

			/* Make sure the directories exist. */
			dirdelim = tmpfilename;
			while ((dirdelim = strchr(dirdelim, '/')) != NULL) {
				*dirdelim = '\0';
				mkdir(tmpfilename, 0755);
				*dirdelim = '/';
				dirdelim++;
			}

			/* We've created the directories. Now retry creating the file. */
			output = fopen(tmpfilename, "w");

			/* 
			 * We had to create the directory. Set up an index.html file for 
			 * the directory where we created our new file.
			 */
			strcpy(indexfilename, filename);
			p = strrchr(indexfilename, '/'); 
			if (p) p++; else p = indexfilename;
			sprintf(p, "index%s", htmlextension);
			sprintf(pagebasename, "%s%s", page->name, htmlextension);
			res = symlink(pagebasename, indexfilename);
			dprintf("Symlinking %s->%s : %d/%d\n", pagebasename, indexfilename, res, errno);

			if (output == NULL) {
				errprintf("Cannot open file %s\n", tmpfilename);
				return;
			}
		}
	}

	setup_htaccess(pagepath);

	headfoot(output, hf_prefix[PAGE_BB], pagepath, "header", page->color);

	if (page->subpages || page->pretitle || defaultpagetitle) {
		/* Print the "Pages hosted locally" header - either the defined pretitle, or the default */
		fprintf(output, "<CENTER><TABLE BORDER=0>\n");
		fprintf(output, "  <TR><TD><br><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", 
			getenv("MKBBTITLE"), 
			(page->pretitle ? page->pretitle : (defaultpagetitle ? defaultpagetitle : getenv("MKBBLOCAL"))));
		fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
		fprintf(output, "</TABLE></CENTER>\n");
	}

	if (!embedded && !hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);
	do_hosts(page->hosts, NULL, output, "", PAGE_BB, pagepath);
	do_groups(page->groups, output, pagepath);
	if (!embedded && hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);

	/* Summaries on main page only */
	if (!embedded && (page->parent == NULL)) {
		do_summaries(dispsums, output);
	}

	/* Extension scripts */
	do_bbext(output, "BBMKBBEXT", "mkbb");

	headfoot(output, hf_prefix[PAGE_BB], pagepath, "footer", page->color);

	if (!embedded) {
		fclose(output);
		if (rename(tmpfilename, filename)) {
			errprintf("Cannot rename %s to %s - error %d\n", tmpfilename, filename, errno);
		}
	}
}


void do_page_with_subs(bbgen_page_t *curpage, dispsummary_t *sums)
{
	bbgen_page_t *levelpage;

	for (levelpage = curpage; (levelpage); levelpage = levelpage->next) {
		do_one_page(levelpage, sums, 0);
		do_page_with_subs(levelpage->subpages, NULL);
	}
}


static void do_bb2ext(FILE *output, char *extenv, char *family)
{
	/*
	 * Do the BB2 page extensions. Since we have built-in
	 * support for eventlog.sh and acklog.sh, we cannot
	 * use the standard do_bbext() routine.
	 */
	char *bbexts, *p;
	FILE *inpipe;
	char extfn[MAX_PATH];
	char buf[4096];
	
	p = getenv(extenv);
	if (p == NULL) {
		/* No extension */
		return;
	}

	bbexts = malcop(p);
	p = strtok(bbexts, "\t ");

	while (p) {
		/* Dont redo the eventlog or acklog things */
		if (strcmp(p, "eventlog.sh") == 0) {
			if (bb2eventlog && !havedoneeventlog) do_eventlog(output, 0, 240, 0);
		}
		else if (strcmp(p, "acklog.sh") == 0) {
			if (bb2acklog && !havedoneacklog) do_acklog(output, 25, 240);
		}
		else {
			sprintf(extfn, "%s/ext/%s/%s", getenv("BBHOME"), family, p);
			inpipe = popen(extfn, "r");
			if (inpipe) {
				while (fgets(buf, sizeof(buf), inpipe)) 
					fputs(buf, output);
				pclose(inpipe);
			}
		}
		p = strtok(NULL, "\t ");
	}

	free(bbexts);
}

int do_bb2_page(char *filename, int summarytype)
{
	bbgen_page_t	bb2page;
	FILE		*output;
	char		*tmpfilename = (char *) malloc(strlen(filename)+5);
	hostlist_t 	*h;

	/* Build a "page" with the hosts that should be included in bb2 page */
	bb2page.name = bb2page.title = "";
	bb2page.color = COL_GREEN;
	bb2page.subpages = NULL;
	bb2page.groups = NULL;
	bb2page.hosts = NULL;
	bb2page.next = NULL;

	for (h=hosthead; (h); h=h->next) {
		entry_t	*e;
		int	useit = 0;

		/*
		 * Why dont we use the interesting_column() * routine here ? 
		 *
		 * Well, because what we are interested in for now is
		 * to determine if this HOST should be included on the page.
		 *
		 * We dont care if individual COLUMNS are included if the 
		 * host shows up - some columns are always included, e.g.
		 * the info- and larrd-columns, but we dont want that to
		 * trigger a host being on the bb2 page!
		 */
		switch (summarytype) {
		  case PAGE_BB2:
			/* Normal BB2 page */
			useit = ((h->hostentry->color == COL_RED) || 
				 (h->hostentry->color == COL_YELLOW) || 
				 (h->hostentry->color == COL_PURPLE));
			break;

		  case PAGE_NK:
			/* The NK page */
			for (useit=0, e=h->hostentry->entries; (e && !useit); e=e->next) {
				useit = (e->alert && (!e->acked) && ((e->color == COL_RED) || ((e->color == COL_YELLOW) && (strcmp(e->column->name, "conn") != 0))));
			}
			break;
		}

		if (useit) {
			host_t *newhost, *walk;

			if (h->hostentry->color > bb2page.color) bb2page.color = h->hostentry->color;

			/* We need to create a copy of the original record, */
			/* as we will diddle with the pointers */
			newhost = (host_t *) malloc(sizeof(host_t));
			memcpy(newhost, h->hostentry, sizeof(host_t));
			newhost->next = NULL;

			/* Insert into sorted host list */
			if ((!bb2page.hosts) || (strcmp(newhost->hostname, bb2page.hosts->hostname) < 0)) {
				/* Empty list, or new entry goes before list head item */
				newhost->next = bb2page.hosts;
				bb2page.hosts = newhost;
			}
			else {
				/* Walk list until we find element that goes after new item */
				for (walk = bb2page.hosts; 
				      (walk->next && (strcmp(newhost->hostname, ((host_t *)walk->next)->hostname) > 0)); 
				      walk = walk->next) ;

				/* "walk" points to element before the new item.
				 *
		 		 * Check for duplicate hosts. We can have a host on two normal BB
		 		 * pages, but in the BB2 page we want it only once.
		 		 */
				if (strcmp(walk->hostname, newhost->hostname) == 0) {
					/* Duplicate at start of list */
					free(newhost);
				}
				else if (walk->next && (strcmp(((host_t *)walk->next)->hostname, newhost->hostname) == 0)) {
					/* Duplicate inside list */
					free(newhost);
				}
				else {
					/* New host */
					newhost->next = walk->next;
					walk->next = newhost;
				}
			}
		}
	}

	sprintf(tmpfilename, "%s.tmp", filename);
	output = fopen(tmpfilename, "w");
	if (output == NULL) {
		errprintf("Cannot open file %s", tmpfilename);
		free(tmpfilename);
		return bb2page.color;
	}

	headfoot(output, hf_prefix[summarytype], "", "header", bb2page.color);

	fprintf(output, "<center>\n");
	fprintf(output, "\n<A NAME=begindata>&nbsp;</A> \n<A NAME=\"hosts-blk\">&nbsp;</A>\n");

	if (bb2page.hosts) {
		do_hosts(bb2page.hosts, NULL, output, "", summarytype, NULL);
	}
	else {
		/* "All Monitored Systems OK */
		fprintf(output, "<FONT SIZE=+2 FACE=\"Arial, Helvetica\"><BR><BR><I>All Monitored Systems OK</I></FONT><BR><BR>");
	}

	if ((snapshot == 0) && (summarytype == PAGE_BB2)) {
		do_bb2ext(output, "BBMKBB2EXT", "mkbb");

		/* Dont redo the eventlog or acklog things */
		if (bb2eventlog && !havedoneeventlog) do_eventlog(output, 0, 240, 0);
		if (bb2acklog && !havedoneacklog) do_acklog(output, 25, 240);
	}

	fprintf(output, "</center>\n");
	headfoot(output, hf_prefix[summarytype], "", "footer", bb2page.color);

	fclose(output);
	if (rename(tmpfilename, filename)) {
		errprintf("Cannot rename %s to %s - error %d\n", tmpfilename, filename, errno);
	}

	free(tmpfilename);

	{
		/* Free temporary hostlist */
		host_t *h1, *h2;

		h1 = bb2page.hosts;
		while (h1) {
			h2 = h1;
			h1 = h1->next;
			free(h2);
		}
	}

	return bb2page.color;
}
