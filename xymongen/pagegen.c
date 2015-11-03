/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* This file contains code to generate the HTML for the Xymon overview        */
/* webpages.                                                                  */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <limits.h>
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

#include "xymongen.h"
#include "util.h"
#include "loadlayout.h"
#include "rssgen.h"
#include "pagegen.h"

int  subpagecolumns = 1;
int  hostsbeforepages = 0;
char *includecolumns = NULL;
char *nongreenignorecolumns = "";
int  nongreennodialups = 0;
int  sort_grouponly_items = 0; /* Standard Xymon behaviour: Don't sort group-only items */
char *rssextension = ".rss"; /* Filename extension for generated RSS files */
char *defaultpagetitle = NULL;
int  pagetitlelinks = 0;
int  pagetextheadings = 0;
int  underlineheadings = 1;
int  maxrowsbeforeheading = 0;
int  showemptygroups = 1;
int  nongreeneventlog = 1;
int  nongreenacklog = 1;
int  nongreeneventlogmaxcount = 100;
int  nongreeneventlogmaxtime = 240;
int  nongreenacklogmaxcount = 25;
int  nongreenacklogmaxtime = 240;
char *logcritstatus = NULL;
int  critonlyreds = 0;
int  wantrss = 0;
int  nongreencolors = ((1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE));

/* Format strings for htaccess files */
char *htaccess = NULL;
char *xymonhtaccess = NULL;
char *xymonpagehtaccess = NULL;
char *xymonsubpagehtaccess = NULL;

char *hf_prefix[3];            /* header/footer prefixes for xymon, nongreen, critical pages*/

static int hostblkidx = 0;

void select_headers_and_footers(char *prefix)
{
	hf_prefix[PAGE_NORMAL]  = (char *) malloc(strlen(prefix)+10); sprintf(hf_prefix[PAGE_NORMAL],  "%snormal",   prefix);
	hf_prefix[PAGE_NONGREEN] = (char *) malloc(strlen(prefix)+10); sprintf(hf_prefix[PAGE_NONGREEN], "%snongreen",  prefix);
	hf_prefix[PAGE_CRITICAL]  = (char *) malloc(strlen(prefix)+10); sprintf(hf_prefix[PAGE_CRITICAL],  "%scritical", prefix);
}


int interesting_column(int pagetype, int color, int alert, xymongen_col_t *column, char *onlycols, char *exceptcols)
{
	/*
	 * Decides if a given column is to be included on a page.
	 */

	if (pagetype == PAGE_NORMAL) {
		/* Fast-path the Xymon page. */

		int result = 1;

		if (onlycols) {
			/* onlycols explicitly list the columns to include (for xymon.html page only) */
			char *search;

			/* loaddata::init_group guarantees that onlycols start and end with a '|' */
			search = (char *) malloc(strlen(column->name)+3);
			sprintf(search, "|%s|", column->name);
			result = (strstr(onlycols, search) != NULL);
			xfree(search);
		}

		if (exceptcols) {
			/* exceptcols explicitly list the columns to exclude (for xymon.html page only) */
			char *search;

			/* loaddata::init_group guarantees that exceptcols start and end with a '|' */
			search = (char *) malloc(strlen(column->name)+3);
			sprintf(search, "|%s|", column->name);
			result = (strstr(exceptcols, search) == NULL);
			xfree(search);
		}

		/* This is final. */
		return result;
	}

	/* pagetype is now known NOT to be PAGE_NORMAL */

	/* CLIENT, TRENDS and INFO columns are always included on non-Xymon pages */
	if (strcmp(column->name, xgetenv("INFOCOLUMN")) == 0) return 1;
	if (strcmp(column->name, xgetenv("TRENDSCOLUMN")) == 0) return 1;
	if (strcmp(column->name, xgetenv("CLIENTCOLUMN")) == 0) return 1;

	if (includecolumns) {
		int result;

		result = (strstr(includecolumns, column->listname) != NULL);

		/* If included, done here. Otherwise may be included further down. */
		if (result) return result;
	}

	switch (pagetype) {
	  case PAGE_NONGREEN:
		  /* Include all non-green tests */
		  if (( (1 << color) & nongreencolors ) != 0) {
			return (strstr(nongreenignorecolumns, column->listname) == NULL);
		  }
		  else return 0;

	  case PAGE_CRITICAL:
		  /* Include only RED or YELLOW tests with "alert" property set. 
		   * Even then, the "conn" test is included only when RED.
		   */
		if (alert) {
			if (color == COL_RED)  return 1;
			if (critonlyreds) return 0;
			if ( (color == COL_YELLOW) || (color == COL_CLEAR) ) {
				if (strcmp(column->name, xgetenv("PINGCOLUMN")) == 0) return 0;
				if (logcritstatus && (strcmp(column->name, logcritstatus) == 0)) return 0;
				return 1;
			}
		}
		break;
	}

	return 0;
}

col_list_t *gen_column_list(host_t *hostlist, int pagetype, char *onlycols, char *exceptcols)
{
	/*
	 * Build a list of all the columns that are in use by
	 * any host in the hostlist passed as parameter.
	 * The column list will be sorted by column name, except 
	 * when doing a "group-only" and the standard Xymon behaviour.
	 */

	col_list_t	*head;
	host_t		*h;
	entry_t		*e;
	col_list_t	*newlistitem, *collist_walk;

	/* Code de-obfuscation trick: Add a null record as the head item */
	/* Simplifies handling since head != NULL and we never have to insert at head of list */
	head = (col_list_t *) calloc(1, sizeof(col_list_t));
	head->column = &null_column;
	head->next = NULL;

	if (!sort_grouponly_items && (onlycols != NULL)) {

		/* 
		 * This is the original handling of "group-only". 
		 * All items are included, whether there are any test data
		 * for a column or not. The order given in the group-only
		 * directive is maintained.
		 * We simple convert the group-only directive to a
		 * col_list_t linked list.
		 */

		char *p1 = onlycols;
		char *p2;
		xymongen_col_t *col;

		collist_walk = head;
		do {
			if (*p1 == '|') p1++;

			p2 = strchr(p1, '|');
			if (p2) {
				*p2 = '\0';

				col = find_or_create_column(p1, 0);
				if (col) {
					newlistitem = (col_list_t *) calloc(1, sizeof(col_list_t));
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
		collist_walk = head; head = head->next; xfree(collist_walk);

		/* We're done - don't even look at the actual test data. */
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
			if (!e->compacted && interesting_column(pagetype, e->color, e->alert, e->column, onlycols, exceptcols)) {
				/* See where e->column should go in list */
				collist_walk = head; 
				while ( (collist_walk->next && 
                               		strcmp(e->column->name, ((col_list_t *)(collist_walk->next))->column->name) > 0) ) {
					collist_walk = collist_walk->next;
				}

				if ((collist_walk->next == NULL) || ((col_list_t *)(collist_walk->next))->column != e->column) {
					/* collist_walk points to the entry before the new one */
					newlistitem = (col_list_t *) calloc(1, sizeof(col_list_t));
					newlistitem->column = e->column;
					newlistitem->next = collist_walk->next;
					collist_walk->next = newlistitem;
				}
			}
		}
	}

	/* Skip the dummy record */
	collist_walk = head; head = head->next; xfree(collist_walk);
	return (head);
}


void setup_htaccess(const char *pagepath)
{
	char htaccessfn[PATH_MAX];
	char htaccesscontent[1024];

	if (htaccess == NULL) return;

	htaccesscontent[0] = '\0';

	if (strlen(pagepath) == 0) {
		sprintf(htaccessfn, "%s", htaccess);
		if (xymonhtaccess) strcpy(htaccesscontent, xymonhtaccess);
	}
	else {
		char *pagename, *subpagename, *p;
		char *path = strdup(pagepath);

		for (p = path + strlen(path) - 1; ((p > path) && (*p == '/')); p--) *p = '\0';

		sprintf(htaccessfn, "%s/%s", path, htaccess);

		pagename = path; if (*pagename == '/') pagename++;
		p = strchr(pagename, '/'); 
		if (p) { 
			*p = '\0'; 
			subpagename = p+1;
			p = strchr(subpagename, '/');
			if (p) *p = '\0';
			if (xymonsubpagehtaccess) sprintf(htaccesscontent, xymonsubpagehtaccess, pagename, subpagename);
		}
		else {
			if (xymonpagehtaccess) sprintf(htaccesscontent, xymonpagehtaccess, pagename);
		}

		xfree(path);
	}

	if (strlen(htaccesscontent)) {
		FILE *fd;
		struct stat st;

		if (stat(htaccessfn, &st) == 0) {
			dbgprintf("htaccess file %s exists, not overwritten\n", htaccessfn);
			return;
		}

		fd = fopen(htaccessfn, "w");
		if (fd) {
			fprintf(fd, "%s\n", htaccesscontent);
			fclose(fd);
		}
		else {
			errprintf("Cannot create %s: %s\n", htaccessfn, strerror(errno));
		}
	}
}

static int host_t_compare(const void *v1, const void *v2)
{
	host_t **n1 = (host_t **)v1;
	host_t **n2 = (host_t **)v2;

	return strcmp((*n1)->hostname, (*n2)->hostname);
}

typedef struct vprec_t {
	char *testname;
	host_t **hosts;
	entry_t **entries;
} vprec_t;

void do_vertical(host_t *head, FILE *output, char *pagepath)
{
	/*
	 * This routine outputs the host part of a page or a group,
	 * but with the hosts going across the page, and the test going down.
	 * I.e. it generates buttons and links to all the hosts for
	 * a test, and the host docs.
	 */

	host_t	*h;
	entry_t	*e;
	char	*xymonskin;
	int	hostcount = 0;
	int	width;
	int	hidx;
	void	*vptree;
	xtreePos_t handle;

	if (head == NULL)
		return;

	vptree = xtreeNew(strcmp);

	xymonskin = strdup(xgetenv("XYMONSKIN"));

	width = atoi(xgetenv("DOTWIDTH"));
	if ((width < 0) || (width > 50)) width = 16;
	width += 4;

	/* Start the table ... */
	fprintf(output, "<CENTER><TABLE SUMMARY=\"Group Block\" BORDER=0 CELLPADDING=2>\n");

	/* output column headings */
	fprintf(output, "<TR><td>&nbsp;</td>");
	for (h = head, hostcount = 0; (h); h = h->next, hostcount++) {
		fprintf(output, " <TD>");
		fprintf(output, " <A HREF=\"%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n", 
			hostsvcurl(h->hostname, xgetenv("INFOCOLUMN"), 1),
			xgetenv("XYMONPAGECOLFONT"), h->hostname);
	}
	fprintf(output, "</TR>\n");
	fprintf(output, "<TR><td>&nbsp;</td><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n\n", hostcount);

	/* Create a tree indexed by the testname, and holding the show/noshow status of each test */
	for (h = head, hidx = 0; (h); h = h->next, hidx++) {
		for (e = h->entries; (e); e = e->next) {
			vprec_t *itm;

			handle = xtreeFind(vptree, e->column->name);
			if (handle == xtreeEnd(vptree)) {
				itm = (vprec_t *)malloc(sizeof(vprec_t));
				itm->testname = e->column->name;
				itm->hosts = (host_t **)calloc(hostcount, sizeof(host_t *));
				itm->entries = (entry_t **)calloc(hostcount, sizeof(entry_t *));
				xtreeAdd(vptree, itm->testname, itm);
			}
			else {
				itm = xtreeData(vptree, handle);
			}

			(itm->hosts)[hidx] = h;
			(itm->entries)[hidx] = e;
		}
	}

	for (handle = xtreeFirst(vptree); (handle != xtreeEnd(vptree)); handle = xtreeNext(vptree, handle)) {
		vprec_t *itm = xtreeData(vptree, handle);

		fprintf(output, "<tr>");
		fprintf(output, "<td valign=center align=left>%s</td>", itm->testname);

		for (hidx = 0; (hidx < hostcount); hidx++) {
			char *skin, *htmlalttag;
			host_t *h = (itm->hosts)[hidx];
			entry_t *e = (itm->entries)[hidx];

			fprintf(output, "<td align=center>");
			if (e == NULL) {
				fprintf(output, "-");
			}
			else {

				if (strcmp(e->column->name, xgetenv("INFOCOLUMN")) == 0) {
					/* show the host ip on the hint display of the "info" column */
					htmlalttag = alttag(e->column->name, COL_GREEN, 0, 1, h->ip);
				}
				else {
					htmlalttag = alttag(e->column->name, e->color, e->acked, e->propagate, e->age);
				}

				skin = (e->skin ? e->skin : xymonskin);

				fprintf(output, "<A HREF=\"%s\">", hostsvcurl(h->hostname, e->column->name, 1));
				fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						skin, dotgiffilename(e->color, e->acked, e->oldage),
						htmlalttag, htmlalttag,
						xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
			}
			fprintf(output, "</td>");
		}

		fprintf(output, "</tr>\n");
	}

	fprintf(output, "</TABLE></CENTER><BR>\n");
	xfree(xymonskin);
}

void do_hosts(host_t *head, int sorthosts, char *onlycols, char *exceptcols, FILE *output, FILE *rssoutput, char *grouptitle, int pagetype, char *pagepath)
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
	char	*xymonskin, *infocolumngif, *trendscolumngif, *clientcolumngif;
	char	*safegrouptitle;
	int	rowcount = 0;
	int     usetooltip = 0;

	if (head == NULL)
		return;

	xymonskin = strdup(xgetenv("XYMONSKIN"));
	infocolumngif = strdup(getenv("INFOCOLUMNGIF") ?  getenv("INFOCOLUMNGIF") : dotgiffilename(COL_GREEN, 0, 1));
	trendscolumngif = strdup(getenv("TRENDSCOLUMNGIF") ?  getenv("TRENDSCOLUMNGIF") : dotgiffilename(COL_GREEN, 0, 1));
	clientcolumngif = strdup(getenv("CLIENTCOLUMNGIF") ?  getenv("CLIENTCOLUMNGIF") : dotgiffilename(COL_GREEN, 0, 1));

	switch (tooltipuse) {
	  case TT_STDONLY: usetooltip = (pagetype == PAGE_NORMAL); break;
	  case TT_ALWAYS: usetooltip = 1; break;
	  case TT_NEVER:  usetooltip = 0; break;
	}

	/* Generate static or dynamic links (from XYMONLOGSTATUS) ? */
	genstatic = generate_static();

	if (hostblkidx == 0) fprintf(output, "<A NAME=hosts-blk>&nbsp;</A>\n\n");
	else fprintf(output, "<A NAME=hosts-blk-%d>&nbsp;</A>\n\n", hostblkidx);
	hostblkidx++;

	if (!grouptitle) grouptitle = "";
	safegrouptitle = stripnonwords(grouptitle);
	if (*safegrouptitle != '\0') fprintf(output, "<A NAME=\"group-%s\"></A>\n\n", safegrouptitle);

	groupcols = gen_column_list(head, pagetype, onlycols, exceptcols);
	for (columncount=0, gc=groupcols; (gc); gc = gc->next, columncount++) ;

	if (showemptygroups || groupcols) {
		int width;

		width = atoi(xgetenv("DOTWIDTH"));
		if ((width < 0) || (width > 50)) width = 16;
		width += 4;

		/* Start the table ... */
		fprintf(output, "<CENTER><TABLE SUMMARY=\"%s Group Block\" BORDER=0 CELLPADDING=2>\n", safegrouptitle);

		/* Generate the host rows */
		if (sorthosts) {
			int i, hcount = 0;
			host_t **hlist;

			for (h=head; (h); h=h->next) hcount++;
			hlist = (host_t **) calloc((hcount+1), sizeof(host_t *));
			for (h=head, i=0; (h); h=h->next, i++) hlist[i] = h;
			qsort(hlist, hcount, sizeof(host_t *), host_t_compare);

			for (h=head=hlist[0], i=1; (i <= hcount); i++) { 
				h->next = hlist[i];
				h = h->next;
			}
			xfree(hlist);
		}

		for (h = head; (h); h = h->next) {
			/* If there is a host pretitle, show it. */
			dbgprintf("Host:%s, pretitle:%s\n", h->hostname, textornull(h->pretitle));

			if (h->pretitle && (pagetype == PAGE_NORMAL)) {
				fprintf(output, "<tr><td colspan=%d align=center valign=middle><br><font %s>%s</font></td></tr>\n", 
						columncount+1, xgetenv("XYMONPAGETITLE"), h->pretitle);
				rowcount = 0;
			}

			if (rowcount == 0) {
				/* output group title and column headings */
				fprintf(output, "<TR>");

				fprintf(output, "<TD VALIGN=MIDDLE ROWSPAN=2><CENTER><FONT %s>%s</FONT></CENTER></TD>\n", 
					xgetenv("XYMONPAGETITLE"), grouptitle);

				for (gc=groupcols; (gc); gc = gc->next) {
					fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
					fprintf(output, " <A HREF=\"%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n", 
						columnlink(gc->column->name), 
						xgetenv("XYMONPAGECOLFONT"), gc->column->name);
				}
				if (columncount) fprintf(output, "</TR>\n<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n\n", columncount);
				else fprintf(output, "</TR>\n<TR><TD></TD></TR>\n\n");
			}

			fprintf(output, "<TR class=line>\n <TD NOWRAP ALIGN=LEFT><A NAME=\"%s\">&nbsp;</A>\n", h->hostname);
			if (maxrowsbeforeheading) rowcount = (rowcount + 1) % maxrowsbeforeheading;
			else rowcount++;

			fprintf(output, "%s", 
				hostnamehtml(h->hostname, 
					     ((pagetype != PAGE_NORMAL) ? hostpage_link(h) : NULL), 
					     usetooltip));

			/* Then the columns. */
			for (gc = groupcols; (gc); gc = gc->next) {
				char *htmlalttag;

				fprintf(output, "<TD ALIGN=CENTER>");

				/* Any column entry for this host ? */
				for (e = h->entries; (e && (e->column != gc->column)); e = e->next) ;
				if (e == NULL) {
					fprintf(output, "-");
				}
				else if (e->histlogname) {
					/* Snapshot points to historical logfile */
					htmlalttag = alttag(e->column->name, e->color, e->acked, e->propagate, e->age);
					fprintf(output, "<A HREF=\"%s\">", 
						histlogurl(h->hostname, e->column->name, 0, e->histlogname));

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						xymonskin, dotgiffilename(e->color, 0, 1),
						htmlalttag, htmlalttag,
						xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
				}
				else if (reportstart == 0) {
					/* Standard webpage */
					char *skin;
					char *img = dotgiffilename(e->color, e->acked, e->oldage);

					if (strcmp(e->column->name, xgetenv("INFOCOLUMN")) == 0) {
						/* Show the host IP on the hint display of the "info" column */
						htmlalttag = alttag(e->column->name, COL_GREEN, 0, 1, h->ip);
						img = infocolumngif;
					}
					else if (strcmp(e->column->name, xgetenv("TRENDSCOLUMN")) == 0) {
						htmlalttag = alttag(e->column->name, COL_GREEN, 0, 1, h->ip);
						img = trendscolumngif;
					}
					else if (strcmp(e->column->name, xgetenv("CLIENTCOLUMN")) == 0) {
						htmlalttag = alttag(e->column->name, COL_GREEN, 0, 1, h->ip);
						img = clientcolumngif;
					}
					else {
						htmlalttag = alttag(e->column->name, e->color, e->acked, e->propagate, e->age);
					}

					skin = (e->skin ? e->skin : xymonskin);

					if (e->sumurl) {
						/* A summary host. */
						fprintf(output, "<A HREF=\"%s\">", e->sumurl);
					}
					else if (genstatic && strcmp(e->column->name, xgetenv("INFOCOLUMN")) && strcmp(e->column->name, xgetenv("TRENDSCOLUMN")) && strcmp(e->column->name, xgetenv("CLIENTCOLUMN"))) {
						/*
						 * Don't use htmlextension here - it's for the
						 * pages generated dynamically.
						 * We don't do static pages for the info- and trends-columns, because
						 * they are always generated dynamically.
						 */
						fprintf(output, "<A HREF=\"%s/html/%s.%s.html\">",
							xgetenv("XYMONWEB"), h->hostname, e->column->name);
						do_rss_item(rssoutput, h, e);
					}
					else {
						fprintf(output, "<A HREF=\"%s\">",
							hostsvcurl(h->hostname, e->column->name, 1));
						do_rss_item(rssoutput, h, e);
					}

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						skin, img,
						htmlalttag, htmlalttag,
						xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
				}
				else {
					/* Report format output */
					if ((e->color == COL_GREEN) || (e->color == COL_CLEAR)) {
						fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0>",
							xymonskin, dotgiffilename(e->color, 0, 1),
							colorname(e->color), colorname(e->color),
							xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
					}
					else {
						if (dynamicreport) {
							fprintf(output, "<A HREF=\"%s\">",
								replogurl(h->hostname, e->column->name, 
									  e->color, 
									  stylenames[reportstyle], use_recentgifs,
									  e->repinfo,
									  h->reporttime, reportend,
									  h->reportwarnlevel));
						}
						else {
							FILE *htmlrep, *textrep;
							char htmlrepfn[PATH_MAX];
							char textrepfn[PATH_MAX];
							char textrepurl[PATH_MAX];

							/* File names are relative - current directory is the output dir */
							/* pagepath is either empty, or it ends with a '/' */
							sprintf(htmlrepfn, "%s%s-%s%s", 
								pagepath, h->hostname, e->column->name, htmlextension);
							sprintf(textrepfn, "%savail-%s-%s.txt",
								pagepath, h->hostname, e->column->name);
							sprintf(textrepurl, "%s/%s", 
								xgetenv("XYMONWEB"), textrepfn);

							htmlrep = fopen(htmlrepfn, "w");
							if (!htmlrep) {
								errprintf("Cannot create output file %s: %s\n",
									htmlrepfn, strerror(errno));
							}
							textrep = fopen(textrepfn, "w");
							if (!textrep) {
								errprintf("Cannot create output file %s: %s\n",
									textrepfn, strerror(errno));
							}

							if (textrep && htmlrep) {
								/* Pre-build the test-specific report */
								restore_replogs(e->causes);
								generate_replog(htmlrep, textrep, textrepurl,
									h->hostname, e->column->name, e->color, reportstyle,
									h->ip, h->displayname,
									reportstart, reportend,
									reportwarnlevel, reportgreenlevel, reportwarnstops,
									e->repinfo);
								fclose(textrep);
								fclose(htmlrep);
							}

							fprintf(output, "<A HREF=\"%s-%s%s\">\n", 
								h->hostname, e->column->name, htmlextension);
						}

						/* Only show #stops if we have this as an SLA parameter */
						if (h->reportwarnstops >= 0) {
							fprintf(output, "<FONT SIZE=-1 COLOR=%s><B>%.2f (%d)</B></FONT></A>\n",
								colorname(e->color), e->repinfo->reportavailability, e->repinfo->reportstops);
						}
						else {
							fprintf(output, "<FONT SIZE=-1 COLOR=%s><B>%.2f</B></FONT></A>\n",
								colorname(e->color), e->repinfo->reportavailability);
						}
					}
				}
				fprintf(output, "</TD>\n");
			}

			fprintf(output, "</TR>\n\n");
		}

		fprintf(output, "</TABLE></CENTER><BR>\n");
	}

	/* Free the columnlist allocated by gen_column_list() */
	while (groupcols) {
		gc = groupcols;
		groupcols = groupcols->next;
		xfree(gc);
	}

	xfree(xymonskin);
	xfree(infocolumngif);
	xfree(trendscolumngif);
}

void do_groups(group_t *head, FILE *output, FILE *rssoutput, char *pagepath)
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
			fprintf(output, "  <TR><TD><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", xgetenv("XYMONPAGETITLE"), g->pretitle);
			if (underlineheadings) fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
			fprintf(output, "</TABLE></CENTER>\n");
		}

		do_hosts(g->hosts, g->sorthosts, g->onlycols, g->exceptcols, output, rssoutput, g->title, PAGE_NORMAL, pagepath);
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
			newhost = init_host(s->row, 1, NULL, NULL, NULL, NULL, 0,0,0,0, 0, 0.0, 0, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);

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
					newentry = (entry_t *) calloc(1, sizeof(entry_t));

					newentry->column = find_or_create_column(s2->column, 1);
					newentry->color = s2->color;
					strcpy(newentry->age, "");
					newentry->oldage = 1; /* Use standard gifs */
					newentry->propagate = 1;
					newentry->sumurl = s2->url;
					newentry->next = newhost->entries;
					newhost->entries = newentry;
				}
			}
		}
	}

	fprintf(output, "<A NAME=\"summaries-blk\">&nbsp;</A>\n");
	fprintf(output, "<CENTER>\n");
	fprintf(output, "<TABLE SUMMARY=\"Summary Block\" BORDER=0>\n");
	fprintf(output, "<TR><TD>\n");
	do_hosts(sumhosts, 1, NULL, NULL, output, NULL, xgetenv("XYMONPAGEREMOTE"), 0, NULL);
	fprintf(output, "</TD></TR>\n");
	fprintf(output, "</TABLE>\n");
	fprintf(output, "</CENTER>\n");
}

void do_page_subpages(FILE *output, xymongen_page_t *subs, char *pagepath)
{
	/*
	 * This routine does NOT generate subpages!
	 * Instead, it generates the LINKS to the subpages below any given page.
	 */

	xymongen_page_t	*p;
	int	currentcolumn;
	char	pagelink[PATH_MAX];
	char    *linkurl;

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
						(2*subpagecolumns + (subpagecolumns - 1)), xgetenv("XYMONPAGETITLE"));
				fprintf(output, "   <br>%s\n", p->pretitle);
				fprintf(output, "</FONT></CENTER></TD></TR>\n");

				fprintf(output, "<TR><TD COLSPAN=%d>", (2*subpagecolumns + (subpagecolumns - 1)));
				if (underlineheadings) {
					fprintf(output, "<HR WIDTH=\"100%%\">");
				}
				else {
					fprintf(output, "&nbsp;");
				}
				fprintf(output, "</TD></TR>\n");
			}

			if (currentcolumn == 0) fprintf(output, "<TR>\n");

			sprintf(pagelink, "%s/%s/%s/%s%s", xgetenv("XYMONWEB"), pagepath, p->name, p->name, htmlextension);

			linkurl = hostlink(p->name);
			fprintf(output, "<TD ALIGN=LEFT><FONT %s>", xgetenv("XYMONPAGEROWFONT"));
			if (linkurl) {
				fprintf(output, "<A HREF=\"%s\">%s</A>", linkurl, p->title);
			}
			else if (pagetitlelinks) {
				fprintf(output, "<A HREF=\"%s\">%s</A>", cleanurl(pagelink), p->title);
			}
			else {
				fprintf(output, "%s", p->title);
			}
			fprintf(output, "</FONT></TD>\n");

			fprintf(output, "<TD><CENTER><A HREF=\"%s\">", cleanurl(pagelink));
			fprintf(output, "<IMG SRC=\"%s/%s\" WIDTH=\"%s\" HEIGHT=\"%s\" BORDER=0 ALT=\"%s\" TITLE=\"%s\"></A>", 
				xgetenv("XYMONSKIN"), dotgiffilename(p->color, 0, ((reportstart > 0) ? 1 : p->oldage)), 
				xgetenv("DOTWIDTH"), xgetenv("DOTHEIGHT"),
				colorname(p->color), colorname(p->color));
			fprintf(output, "</CENTER></TD>\n");

			if (currentcolumn == (subpagecolumns-1)) {
				fprintf(output, "</TR>\n");
				currentcolumn = 0;
			}
			else {
				/* Need to have a little space between columns */
				fprintf(output, "<TD WIDTH=\"%s\">&nbsp;</TD>", xgetenv("DOTWIDTH"));
				currentcolumn++;
			}
		}

		if (currentcolumn != 0) fprintf(output, "</TR>\n");

		fprintf(output, "</TABLE><BR>\n");
		fprintf(output, "</CENTER>\n");
	}
}


void do_one_page(xymongen_page_t *page, dispsummary_t *sums, int embedded)
{
	FILE	*output = NULL;
	FILE	*rssoutput = NULL;
	char	pagepath[PATH_MAX];
	char	filename[PATH_MAX];
	char	tmpfilename[PATH_MAX];
	char	rssfilename[PATH_MAX];
	char	tmprssfilename[PATH_MAX];
	char	curdir[PATH_MAX];
	char	*dirdelim;
	char	*localtext;

	if (!getcwd(curdir, sizeof(curdir))) {
		errprintf("Cannot get current directory: %s\n", strerror(errno));
		return;
	}
	localtext = strdup(xgetenv((page->parent ? "XYMONPAGESUBLOCAL" : "XYMONPAGELOCAL")));

	pagepath[0] = '\0';
	if (embedded) {
		output = stdout;
	}
	else {
		if (page->parent == NULL) {
			char	indexfilename[PATH_MAX];

			/* top level page */
			sprintf(filename, "xymon%s", htmlextension);
			sprintf(rssfilename, "xymon%s", rssextension);
			sprintf(indexfilename, "index%s", htmlextension);
			unlink(indexfilename); 
			if (symlink(filename, indexfilename)) {
				dbgprintf("Symlinking %s -> %s\n", filename, indexfilename);
			}
		}
		else {
			char tmppath[PATH_MAX];
			xymongen_page_t *pgwalk;
	
			for (pgwalk = page; (pgwalk); pgwalk = pgwalk->parent) {
				if (strlen(pgwalk->name)) {
					sprintf(tmppath, "%s/%s/", pgwalk->name, pagepath);
					strcpy(pagepath, tmppath);
				}
			}
	
			sprintf(filename, "%s/%s%s", pagepath, page->name, htmlextension);
			sprintf(rssfilename, "%s/%s%s", pagepath, page->name, rssextension);
		}
		sprintf(tmpfilename, "%s.tmp", filename);
		sprintf(tmprssfilename, "%s.tmp", rssfilename);


		/* Try creating the output file. If it fails, we may need to create the directories */
		hostblkidx = 0;
		output = fopen(tmpfilename, "w");
		if (output == NULL) {
			char indexfilename[PATH_MAX];
			char pagebasename[PATH_MAX];
			char *p;
			int res;

			/* Make sure the directories exist. */
			dirdelim = tmpfilename;
			while ((dirdelim = strchr(dirdelim, '/')) != NULL) {
				*dirdelim = '\0';
				if ((mkdir(tmpfilename, 0755) == -1) && (errno != EEXIST)) {
					errprintf("Cannot create directory %s (in %s): %s\n", 
						   tmpfilename, curdir, strerror(errno));
				}
				*dirdelim = '/';
				dirdelim++;
			}

			/* We've created the directories. Now retry creating the file. */
			output = fopen(tmpfilename, "w");
			if (output == NULL) {
				errprintf("Cannot create file %s (in %s): %s\n", 
					  tmpfilename, curdir, strerror(errno));
				return;
			}

			/* 
			 * We had to create the directory. Set up an index.html file for 
			 * the directory where we created our new file.
			 */
			strcpy(indexfilename, filename);
			p = strrchr(indexfilename, '/'); 
			if (p) p++; else p = indexfilename;
			sprintf(p, "index%s", htmlextension);
			sprintf(pagebasename, "%s%s", page->name, htmlextension);
			if ((symlink(pagebasename, indexfilename) == -1) && ((res = errno) != EEXIST)) {
				errprintf("Cannot create symlink %s->%s (in %s): %s\n", 
					  indexfilename, pagebasename, curdir, strerror(res));
			}

			if (output == NULL) {
				return;
			}
		}

		if (wantrss) {
			/* Just create the RSS files - all the directory stuff is done */
			rssoutput = fopen(tmprssfilename, "w");
			if (rssoutput == NULL) {
				errprintf("Cannot open RSS file %s: %s\n", tmprssfilename, strerror(errno));
			}
		}
	}

	setup_htaccess(pagepath);

	headfoot(output, hf_prefix[PAGE_NORMAL], pagepath, "header", page->color);
	do_rss_header(rssoutput);

	if (pagetextheadings && page->title && strlen(page->title)) {
		fprintf(output, "<CENTER><TABLE BORDER=0>\n");
		fprintf(output, "  <TR><TD><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", 
			xgetenv("XYMONPAGETITLE"), page->title);
		if (underlineheadings) fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
		fprintf(output, "</TABLE></CENTER>\n");
	}
	else if (page->subpages) {
		/* If first page does not have a pretitle, use the default ones */
		if (page->subpages->pretitle == NULL) {
			page->subpages->pretitle = (defaultpagetitle ? defaultpagetitle : localtext);
		}
	}

	if (!embedded && !hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);
	if (page->vertical) {
		do_vertical(page->hosts, output, pagepath);
	}
	else {
		do_hosts(page->hosts, 0, NULL, NULL, output, rssoutput, "", PAGE_NORMAL, pagepath);
		do_groups(page->groups, output, rssoutput, pagepath);
	}
	if (!embedded && hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);

	/* Summaries on main page only */
	if (!embedded && (page->parent == NULL)) {
		do_summaries(dispsums, output);
	}

	/* Extension scripts */
	do_extensions(output, "XYMONSTDEXT", "mkbb");

	headfoot(output, hf_prefix[PAGE_NORMAL], pagepath, "footer", page->color);
	do_rss_footer(rssoutput);

	if (!embedded) {
		fclose(output);
		if (rename(tmpfilename, filename)) {
			errprintf("Cannot rename %s to %s - error %d\n", tmpfilename, filename, errno);
		}
		if (rssoutput) {
			fclose(rssoutput);
			if (rename(tmprssfilename, rssfilename)) {
				errprintf("Cannot rename %s to %s - error %d\n", tmprssfilename, rssfilename, errno);
			}
		}
	}

	xfree(localtext);
}


void do_page_with_subs(xymongen_page_t *curpage, dispsummary_t *sums)
{
	xymongen_page_t *levelpage;

	for (levelpage = curpage; (levelpage); levelpage = levelpage->next) {
		do_one_page(levelpage, sums, 0);
		do_page_with_subs(levelpage->subpages, NULL);
	}
}


static void do_nongreenext(FILE *output, char *extenv, char *family)
{
	/*
	 * Do the non-green page extensions. Since we have built-in
	 * support for eventlog.sh and acklog.sh, we cannot
	 * use the standard do_extensions() routine.
	 */
	char *extensions, *p;
	FILE *inpipe;
	char extfn[PATH_MAX];
	char buf[4096];
	
	p = xgetenv(extenv);
	if (p == NULL) {
		/* No extension */
		return;
	}

	extensions = strdup(p);
	p = strtok(extensions, "\t ");

	while (p) {
		/* Don't redo the eventlog or acklog things */
		if (strcmp(p, "eventlog.sh") == 0) {
			if (nongreeneventlog && !havedoneeventlog) {
				do_eventlog(output, nongreeneventlogmaxcount, nongreeneventlogmaxtime,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, nongreennodialups, 
				host_exists,
				NULL, NULL, NULL, XYMON_COUNT_NONE, XYMON_S_NONE, NULL);
			}
		}
		else if (strcmp(p, "acklog.sh") == 0) {
			if (nongreenacklog && !havedoneacklog) do_acklog(output, nongreenacklogmaxcount, nongreenacklogmaxtime);
		}
		else if (strcmp(p, "summaries") == 0) {
			do_summaries(dispsums, output);
		}
		else {
			sprintf(extfn, "%s/ext/%s/%s", xgetenv("XYMONHOME"), family, p);
			inpipe = popen(extfn, "r");
			if (inpipe) {
				while (fgets(buf, sizeof(buf), inpipe)) 
					fputs(buf, output);
				pclose(inpipe);
			}
		}
		p = strtok(NULL, "\t ");
	}

	xfree(extensions);
}

int do_nongreen_page(char *nssidebarfilename, int summarytype, char *filenamebase)
{
	xymongen_page_t	nongreenpage;
	FILE		*output = NULL;
	FILE		*rssoutput = NULL;
	char		filename[PATH_MAX];
	char		tmpfilename[PATH_MAX];
	char		rssfilename[PATH_MAX];
	char		tmprssfilename[PATH_MAX];
	hostlist_t 	*h;

	/* Build a "page" with the hosts that should be included in nongreen page */
	nongreenpage.name = nongreenpage.title = "";
	nongreenpage.color = COL_GREEN;
	nongreenpage.subpages = NULL;
	nongreenpage.groups = NULL;
	nongreenpage.hosts = NULL;
	nongreenpage.next = NULL;

	for (h=hostlistBegin(); (h); h=hostlistNext()) {
		entry_t	*e;
		int	useit = 0;

		/*
		 * Why don't we use the interesting_column() routine here ? 
		 *
		 * Well, because what we are interested in for now is
		 * to determine if this HOST should be included on the page.
		 *
		 * We don't care if individual COLUMNS are included if the 
		 * host shows up - some columns are always included, e.g.
		 * the info- and trends-columns, but we don't want that to
		 * trigger a host being on the nongreen page!
		 */
		switch (summarytype) {
		  case PAGE_NONGREEN:
			/* Normal non-green page */
			if (h->hostentry->nonongreen || (nongreennodialups && h->hostentry->dialup)) 
				useit = 0;
			else
				useit = (( (1 << h->hostentry->nongreencolor) & nongreencolors ) != 0);
			break;

		  case PAGE_CRITICAL:
			/* The Critical page */
			for (useit=0, e=h->hostentry->entries; (e && !useit); e=e->next) {
				if (e->alert && !e->acked) {
					if (e->color == COL_RED) {
						useit = 1;
					}
					else {
						if (!critonlyreds) {
							useit = ((e->color == COL_YELLOW) && (strcmp(e->column->name, xgetenv("PINGCOLUMN")) != 0));
						}
					}
				}
			}
			break;
		}

		if (useit) {
			host_t *newhost, *walk;

			switch (summarytype) {
			  case PAGE_NONGREEN:
				if (h->hostentry->nongreencolor > nongreenpage.color) nongreenpage.color = h->hostentry->nongreencolor;
				break;
			  case PAGE_CRITICAL:
				if (h->hostentry->criticalcolor > nongreenpage.color) nongreenpage.color = h->hostentry->criticalcolor;
				break;
			}

			/* We need to create a copy of the original record, */
			/* as we will diddle with the pointers */
			newhost = (host_t *) calloc(1, sizeof(host_t));
			memcpy(newhost, h->hostentry, sizeof(host_t));
			newhost->next = NULL;

			/* Insert into sorted host list */
			if ((!nongreenpage.hosts) || (strcmp(newhost->hostname, nongreenpage.hosts->hostname) < 0)) {
				/* Empty list, or new entry goes before list head item */
				newhost->next = nongreenpage.hosts;
				nongreenpage.hosts = newhost;
			}
			else {
				/* Walk list until we find element that goes after new item */
				for (walk = nongreenpage.hosts; 
				      (walk->next && (strcmp(newhost->hostname, ((host_t *)walk->next)->hostname) > 0)); 
				      walk = walk->next) ;

				/* "walk" points to element before the new item.
				 *
		 		 * Check for duplicate hosts. We can have a host on two normal Xymon
		 		 * pages, but in the non-green page we want it only once.
		 		 */
				if (strcmp(walk->hostname, newhost->hostname) == 0) {
					/* Duplicate at start of list */
					xfree(newhost);
				}
				else if (walk->next && (strcmp(((host_t *)walk->next)->hostname, newhost->hostname) == 0)) {
					/* Duplicate inside list */
					xfree(newhost);
				}
				else {
					/* New host */
					newhost->next = walk->next;
					walk->next = newhost;
				}
			}
		}
	}

	switch (summarytype) {
	  case PAGE_NONGREEN:
		sprintf(filename, "%s%s", filenamebase, htmlextension);
		sprintf(rssfilename, "%s%s", filenamebase, rssextension);
		break;
	  case PAGE_CRITICAL:
		sprintf(filename, "%s%s", filenamebase, htmlextension);
		sprintf(rssfilename, "%s%s", filenamebase, rssextension);
		break;
	}

	sprintf(tmpfilename, "%s.tmp", filename);
	output = fopen(tmpfilename, "w");
	if (output == NULL) {
		errprintf("Cannot create file %s: %s\n", tmpfilename, strerror(errno));
		return nongreenpage.color;
	}

	if (wantrss) {
		sprintf(tmprssfilename, "%s.tmp", rssfilename);
		rssoutput = fopen(tmprssfilename, "w");
		if (rssoutput == NULL) {
			errprintf("Cannot create RSS file %s: %s\n", tmpfilename, strerror(errno));
			return nongreenpage.color;
		}
	}

	headfoot(output, hf_prefix[summarytype], "", "header", nongreenpage.color);
	do_rss_header(rssoutput);

	fprintf(output, "<center>\n");
	fprintf(output, "\n<A NAME=begindata>&nbsp;</A> \n<A NAME=\"hosts-blk\">&nbsp;</A>\n");

	if (nongreenpage.hosts) {
		do_hosts(nongreenpage.hosts, 0, NULL, NULL, output, rssoutput, "", summarytype, NULL);
	}
	else {
		/* All Monitored Systems OK */
		fprintf(output, "%s", xgetenv("XYMONALLOKTEXT"));
	}

	/* Summaries on nongreenpage as well */
	do_summaries(dispsums, output);

	if ((snapshot == 0) && (summarytype == PAGE_NONGREEN)) {
		do_nongreenext(output, "XYMONNONGREENEXT", "mkbb");

		/* Don't redo the eventlog or acklog things */
		if (nongreeneventlog && !havedoneeventlog) {
			do_eventlog(output, nongreeneventlogmaxcount, nongreeneventlogmaxtime, 
				    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, nongreennodialups, 
				    host_exists,
				    NULL, NULL, NULL, XYMON_COUNT_NONE, XYMON_S_NONE, NULL);
		}
		if (nongreenacklog && !havedoneacklog) do_acklog(output, nongreenacklogmaxcount, nongreenacklogmaxtime);
	}

	fprintf(output, "</center>\n");
	headfoot(output, hf_prefix[summarytype], "", "footer", nongreenpage.color);
	do_rss_footer(rssoutput);

	fclose(output);
	if (rename(tmpfilename, filename)) {
		errprintf("Cannot rename %s to %s - error %d\n", tmpfilename, filename, errno);
	}

	if (rssoutput) {
		fclose(rssoutput);
		if (rename(tmprssfilename, rssfilename)) {
			errprintf("Cannot rename %s to %s - error %d\n", tmprssfilename, rssfilename, errno);
		}
	}

	if (nssidebarfilename) do_netscape_sidebar(nssidebarfilename, nongreenpage.hosts);

	if (logcritstatus && (summarytype == PAGE_CRITICAL)) {
		host_t *hwalk;
		entry_t *ewalk;
		char *msgptr;
		char msgline[MAX_LINE_LEN];
		FILE *nklog;
		char nklogfn[PATH_MAX];
		char svcspace;

		sprintf(nklogfn, "%s/criticalstatus.log", xgetenv("XYMONSERVERLOGS"));
		nklog = fopen(nklogfn, "a");
		if (nklog == NULL) {
			errprintf("Cannot log Critical status to %s: %s\n", nklogfn, strerror(errno));
		}

		init_timestamp();
		combo_start();
		init_status(nongreenpage.color);
		sprintf(msgline, "status %s.%s %s %s Critical page %s\n\n", xgetenv("MACHINE"), 
			logcritstatus, colorname(nongreenpage.color), timestamp, colorname(nongreenpage.color));
		addtostatus(msgline);

		if (nklog) fprintf(nklog, "%u\t%s", (unsigned int)getcurrenttime(NULL), colorname(nongreenpage.color));

		for (hwalk = nongreenpage.hosts; hwalk; hwalk = hwalk->next) {
			msgptr = msgline;
			msgptr += sprintf(msgline, "&%s %s :", colorname(hwalk->color), hwalk->hostname);
			if (nklog) fprintf(nklog, "\t%s ", hwalk->hostname);
			svcspace = '(';

			for (ewalk = hwalk->entries; (ewalk); ewalk = ewalk->next) {
				if ((summarytype == PAGE_NONGREEN) || (ewalk->alert)) {
					if ((ewalk->color == COL_RED) || (ewalk->color == COL_YELLOW)) {
						msgptr += sprintf(msgptr, "%s", ewalk->column->name);
						if (nklog) fprintf(nklog, "%c%s:%s", svcspace, ewalk->column->name, colorname(ewalk->color));
						svcspace = ' ';
					}
				}
			}
			strcpy(msgptr, "\n");
			addtostatus(msgline);

			if (nklog) fprintf(nklog, ")");
		}
		finish_status();
		combo_end();

		if (nklog) {
			fprintf(nklog, "\n");
			fclose(nklog);
		}
	}

	{
		/* Free temporary hostlist */
		host_t *h1, *h2;

		h1 = nongreenpage.hosts;
		while (h1) {
			h2 = h1;
			h1 = h1->next;
			xfree(h2);
		}
	}

	return nongreenpage.color;
}
