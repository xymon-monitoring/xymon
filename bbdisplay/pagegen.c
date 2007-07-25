/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* This file contains code to generate the HTML for the Hobbit overview       */
/* webpages.                                                                  */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: pagegen.c,v 1.186 2007-07-25 20:02:06 henrik Exp $";

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

#include "bbgen.h"
#include "util.h"
#include "loadbbhosts.h"
#include "rssgen.h"
#include "pagegen.h"

int  subpagecolumns = 1;
int  hostsbeforepages = 0;
char *includecolumns = NULL;
char *bb2ignorecolumns = "";
int  bb2nodialups = 0;
int  sort_grouponly_items = 0; /* Standard BB behaviour: Dont sort group-only items */
char *rssextension = ".rss"; /* Filename extension for generated RSS files */
char *defaultpagetitle = NULL;
int  pagetitlelinks = 0;
int  pagetextheadings = 0;
int  underlineheadings = 1;
int  maxrowsbeforeheading = 0;
int  bb2eventlog = 1;
int  bb2acklog = 1;
int  bb2eventlogmaxcount = 100;
int  bb2eventlogmaxtime = 240;
int  bb2acklogmaxcount = 25;
int  bb2acklogmaxtime = 240;
char *lognkstatus = NULL;
int  nkonlyreds = 0;
char *nkackname = "NK";
int  wantrss = 0;
int  bb2colors = ((1 << COL_RED) | (1 << COL_YELLOW) | (1 << COL_PURPLE));

/* Format strings for htaccess files */
char *htaccess = NULL;
char *bbhtaccess = NULL;
char *bbpagehtaccess = NULL;
char *bbsubpagehtaccess = NULL;

char *hf_prefix[3];            /* header/footer prefixes for BB, BB2, BBNK pages*/

static int hostblkidx = 0;

void select_headers_and_footers(char *prefix)
{
	hf_prefix[PAGE_BB]  = (char *) malloc(strlen(prefix)+1); sprintf(hf_prefix[PAGE_BB],  "%s",   prefix);
	hf_prefix[PAGE_BB2] = (char *) malloc(strlen(prefix)+2); sprintf(hf_prefix[PAGE_BB2], "%s2",  prefix);
	hf_prefix[PAGE_NK]  = (char *) malloc(strlen(prefix)+3); sprintf(hf_prefix[PAGE_NK],  "%snk", prefix);
}


int interesting_column(int pagetype, int color, int alert, bbgen_col_t *column, char *onlycols, char *exceptcols)
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
			search = (char *) malloc(strlen(column->name)+3);
			sprintf(search, "|%s|", column->name);
			result = (strstr(onlycols, search) != NULL);
			xfree(search);
		}

		if (exceptcols) {
			/* exceptcols explicitly list the columns to exclude (for bb page only) */
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

	/* pagetype is now known NOT to be PAGE_BB */

	/* TRENDS and INFO columns are always included on non-BB pages */
	if (strcmp(column->name, xgetenv("INFOCOLUMN")) == 0) return 1;
	if (strcmp(column->name, xgetenv("TRENDSCOLUMN")) == 0) return 1;

	if (includecolumns) {
		int result;

		result = (strstr(includecolumns, column->listname) != NULL);

		/* If included, done here. Otherwise may be included further down. */
		if (result) return result;
	}

	switch (pagetype) {
	  case PAGE_BB2:
		  /* Include all non-green tests */
		  if (( (1 << color) & bb2colors ) != 0) {
			return (strstr(bb2ignorecolumns, column->listname) == NULL);
		  }
		  else return 0;

	  case PAGE_NK:
		  /* Include only RED or YELLOW tests with "alert" property set. 
		   * Even then, the "conn" test is included only when RED.
		   */
		if (alert) {
			if (color == COL_RED)  return 1;
			if (nkonlyreds) return 0;
			if ( (color == COL_YELLOW) || (color == COL_CLEAR) ) {
				if (strcmp(column->name, xgetenv("PINGCOLUMN")) == 0) return 0;
				if (lognkstatus && (strcmp(column->name, lognkstatus) == 0)) return 0;
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
	 * when doing a "group-only" and the standard BB behaviour.
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
			if (interesting_column(pagetype, e->color, e->alert, e->column, onlycols, exceptcols)) {
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
		if (bbhtaccess) strcpy(htaccesscontent, bbhtaccess);
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
			if (bbsubpagehtaccess) sprintf(htaccesscontent, bbsubpagehtaccess, pagename, subpagename);
		}
		else {
			if (bbpagehtaccess) sprintf(htaccesscontent, bbpagehtaccess, pagename);
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

static int host_t_compare(void *a, void *b)
{
	return (strcmp(((host_t *)a)->hostname, ((host_t *)b)->hostname) < 0);
}

static void * host_t_getnext(void *a)
{
	return ((host_t *)a)->next;
}

static void host_t_setnext(void *a, void *newval)
{
	((host_t *)a)->next = (host_t *)newval;
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
	char	*bbskin;
	int	rowcount = 0;

	if (head == NULL)
		return;

	bbskin = strdup(xgetenv("BBSKIN"));

	/* Generate static or dynamic links (from BBLOGSTATUS) ? */
	genstatic = generate_static();

	if (hostblkidx == 0) fprintf(output, "<A NAME=hosts-blk>&nbsp;</A>\n\n");
	else fprintf(output, "<A NAME=hosts-blk-%d>&nbsp;</A>\n\n", hostblkidx);
	hostblkidx++;

	groupcols = gen_column_list(head, pagetype, onlycols, exceptcols);
	for (columncount=0, gc=groupcols; (gc); gc = gc->next, columncount++) ;

	if (groupcols) {
		int width;

		width = atoi(xgetenv("DOTWIDTH"));
		if ((width < 0) || (width > 50)) width = 16;
		width += 4;

		/* Start the table ... */
		fprintf(output, "<CENTER><TABLE SUMMARY=\"Group Block\" BORDER=0 CELLPADDING=2>\n");

		/* Generate the host rows */
		if (sorthosts) msort(head, host_t_compare, host_t_getnext, host_t_setnext);
		for (h = head; (h); h = h->next) {
			/* If there is a host pretitle, show it. */
			dbgprintf("Host:%s, pretitle:%s\n", h->hostname, textornull(h->pretitle));

			if (h->pretitle) {
				fprintf(output, "<tr><td colspan=%d align=center valign=middle><br><font %s>%s</font></td></tr>\n", 
						columncount+1, xgetenv("MKBBTITLE"), h->pretitle);
				rowcount = 0;
			}

			if (rowcount == 0) {
				/* output group title and column headings */
				fprintf(output, "<TR>");

				fprintf(output, "<TD VALIGN=MIDDLE ROWSPAN=2><CENTER><FONT %s>%s</FONT></CENTER></TD>\n", 
					xgetenv("MKBBTITLE"), (strlen(grouptitle) ? grouptitle : "&nbsp;"));

				for (gc=groupcols; (gc); gc = gc->next) {
					fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
					fprintf(output, " <A HREF=\"%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n", 
						columnlink(gc->column->name), 
						xgetenv("MKBBCOLFONT"), gc->column->name);
				}
				fprintf(output, "</TR> \n<TR><TD COLSPAN=%d><HR WIDTH=\"100%%\"></TD></TR>\n\n", columncount);
			}

			fprintf(output, "<TR>\n <TD NOWRAP><A NAME=\"%s\">&nbsp;</A>\n", h->hostname);
			if (maxrowsbeforeheading) rowcount = (rowcount + 1) % maxrowsbeforeheading;
			else rowcount++;

			fprintf(output, "%s", 
				hostnamehtml(h->hostname, 
					     ((pagetype != PAGE_BB) ? hostpage_link(h) : NULL), 
					     (pagetype == PAGE_BB) ) );

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
						bbskin, dotgiffilename(e->color, 0, 1),
						htmlalttag, htmlalttag,
						xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
				}
				else if (reportstart == 0) {
					/* Standard webpage */
					char *skin;

					if (strcmp(e->column->name, xgetenv("INFOCOLUMN")) == 0) {
						/* Show the host IP on the hint display of the "info" column */
						htmlalttag = alttag(e->column->name, COL_GREEN, 0, 1, h->ip);
					}
					else {
						htmlalttag = alttag(e->column->name, e->color, e->acked, e->propagate, e->age);
					}

					skin = (e->skin ? e->skin : bbskin);

					if (e->sumurl) {
						/* A summary host. */
						fprintf(output, "<A HREF=\"%s\">", e->sumurl);
					}
					else if (genstatic && strcmp(e->column->name, xgetenv("INFOCOLUMN")) && strcmp(e->column->name, xgetenv("TRENDSCOLUMN"))) {
						/*
						 * Dont use htmlextension here - it's for the
						 * pages generated by bbd.
						 * We dont do static pages for the info- and trends-columns, because
						 * they are always generated dynamically.
						 */
						fprintf(output, "<A HREF=\"%s/html/%s.%s.html\">",
							xgetenv("BBWEB"), h->hostname, e->column->name);
						do_rss_item(rssoutput, h, e);
					}
					else {
						fprintf(output, "<A HREF=\"%s\">",
							hostsvcurl(h->hostname, e->column->name, 1));
						do_rss_item(rssoutput, h, e);
					}

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						skin, dotgiffilename(e->color, e->acked, e->oldage),
						htmlalttag, htmlalttag,
						xgetenv("DOTHEIGHT"), xgetenv("DOTWIDTH"));
				}
				else {
					/* Report format output */
					if ((e->color == COL_GREEN) || (e->color == COL_CLEAR)) {
						fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" TITLE=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0>",
							bbskin, dotgiffilename(e->color, 0, 1),
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
								xgetenv("BBWEB"), textrepfn);

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
									reportwarnlevel, reportgreenlevel, e->repinfo);
								fclose(textrep);
								fclose(htmlrep);
							}

							fprintf(output, "<A HREF=\"%s-%s%s\">\n", 
								h->hostname, e->column->name, htmlextension);
						}
						fprintf(output, "<FONT SIZE=-1 COLOR=%s><B>%.2f</B></FONT></A>\n",
							colorname(e->color), e->repinfo->reportavailability);
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

	xfree(bbskin);
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
			fprintf(output, "  <TR><TD><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", xgetenv("MKBBTITLE"), g->pretitle);
			if (underlineheadings) fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
			fprintf(output, "</TABLE></CENTER>\n");
		}

		do_hosts(g->hosts, g->sorthosts, g->onlycols, g->exceptcols, output, rssoutput, g->title, PAGE_BB, pagepath);
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
			newhost = init_host(s->row, 1, NULL, NULL, NULL, NULL, 0,0,0,0, 0, 0.0, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);

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
	do_hosts(sumhosts, 1, NULL, NULL, output, NULL, xgetenv("MKBBREMOTE"), 0, NULL);
	fprintf(output, "</TD></TR>\n");
	fprintf(output, "</TABLE>\n");
	fprintf(output, "</CENTER>\n");
}

void do_page_subpages(FILE *output, bbgen_page_t *subs, char *pagepath)
{
	/*
	 * This routine does NOT generate subpages!
	 * Instead, it generates the LINKS to the subpages below any given page.
	 */

	bbgen_page_t	*p;
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
						(2*subpagecolumns + (subpagecolumns - 1)), xgetenv("MKBBTITLE"));
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

			sprintf(pagelink, "%s/%s/%s/%s%s", xgetenv("BBWEB"), pagepath, p->name, p->name, htmlextension);

			linkurl = hostlink(p->name);
			fprintf(output, "<TD><FONT %s>", xgetenv("MKBBROWFONT"));
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
				xgetenv("BBSKIN"), dotgiffilename(p->color, 0, ((reportstart > 0) ? 1 : p->oldage)), 
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


void do_one_page(bbgen_page_t *page, dispsummary_t *sums, int embedded)
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
	char	*mkbblocal;

	getcwd(curdir, sizeof(curdir));
	mkbblocal = strdup(xgetenv((page->parent ? "MKBBSUBLOCAL" : "MKBBLOCAL")));

	pagepath[0] = '\0';
	if (embedded) {
		output = stdout;
	}
	else {
		if (page->parent == NULL) {
			char	indexfilename[PATH_MAX];

			/* top level page */
			sprintf(filename, "bb%s", htmlextension);
			sprintf(rssfilename, "bb%s", rssextension);
			sprintf(indexfilename, "index%s", htmlextension);
			symlink(filename, indexfilename);
			dbgprintf("Symlinking %s -> %s\n", filename, indexfilename);
		}
		else {
			char tmppath[PATH_MAX];
			bbgen_page_t *pgwalk;
	
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

	headfoot(output, hf_prefix[PAGE_BB], pagepath, "header", page->color);
	do_rss_header(rssoutput);

	if (pagetextheadings && page->title && strlen(page->title)) {
		fprintf(output, "<CENTER><TABLE BORDER=0>\n");
		fprintf(output, "  <TR><TD><CENTER><FONT %s>%s</FONT></CENTER></TD></TR>\n", 
			xgetenv("MKBBTITLE"), page->title);
		if (underlineheadings) fprintf(output, "  <TR><TD><HR WIDTH=\"100%%\"></TD></TR>\n");
		fprintf(output, "</TABLE></CENTER>\n");
	}
	else if (page->subpages) {
		/* If first page does not have a pretitle, use the default ones */
		if (page->subpages->pretitle == NULL) {
			page->subpages->pretitle = (defaultpagetitle ? defaultpagetitle : mkbblocal);
		}
	}

	if (!embedded && !hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);
	do_hosts(page->hosts, 0, NULL, NULL, output, rssoutput, "", PAGE_BB, pagepath);
	do_groups(page->groups, output, rssoutput, pagepath);
	if (!embedded && hostsbeforepages && page->subpages) do_page_subpages(output, page->subpages, pagepath);

	/* Summaries on main page only */
	if (!embedded && (page->parent == NULL)) {
		do_summaries(dispsums, output);
	}

	/* Extension scripts */
	do_bbext(output, "BBMKBBEXT", "mkbb");

	headfoot(output, hf_prefix[PAGE_BB], pagepath, "footer", page->color);
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

	xfree(mkbblocal);
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
	char extfn[PATH_MAX];
	char buf[4096];
	
	p = xgetenv(extenv);
	if (p == NULL) {
		/* No extension */
		return;
	}

	bbexts = strdup(p);
	p = strtok(bbexts, "\t ");

	while (p) {
		/* Dont redo the eventlog or acklog things */
		if (strcmp(p, "eventlog.sh") == 0) {
			if (bb2eventlog && !havedoneeventlog) {
				do_eventlog(output, bb2eventlogmaxcount, bb2eventlogmaxtime,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, bb2nodialups, 
				host_exists,
				NULL, NULL, NULL, S_NONE);
			}
		}
		else if (strcmp(p, "acklog.sh") == 0) {
			if (bb2acklog && !havedoneacklog) do_acklog(output, bb2acklogmaxcount, bb2acklogmaxtime);
		}
		else {
			sprintf(extfn, "%s/ext/%s/%s", xgetenv("BBHOME"), family, p);
			inpipe = popen(extfn, "r");
			if (inpipe) {
				while (fgets(buf, sizeof(buf), inpipe)) 
					fputs(buf, output);
				pclose(inpipe);
			}
		}
		p = strtok(NULL, "\t ");
	}

	xfree(bbexts);
}

int do_bb2_page(char *nssidebarfilename, int summarytype)
{
	bbgen_page_t	bb2page;
	FILE		*output = NULL;
	FILE		*rssoutput = NULL;
	char		filename[PATH_MAX];
	char		tmpfilename[PATH_MAX];
	char		rssfilename[PATH_MAX];
	char		tmprssfilename[PATH_MAX];
	hostlist_t 	*h;

	/* Build a "page" with the hosts that should be included in bb2 page */
	bb2page.name = bb2page.title = "";
	bb2page.color = COL_GREEN;
	bb2page.subpages = NULL;
	bb2page.groups = NULL;
	bb2page.hosts = NULL;
	bb2page.next = NULL;

	for (h=hostlistBegin(); (h); h=hostlistNext()) {
		entry_t	*e;
		int	useit = 0;

		/*
		 * Why dont we use the interesting_column() routine here ? 
		 *
		 * Well, because what we are interested in for now is
		 * to determine if this HOST should be included on the page.
		 *
		 * We dont care if individual COLUMNS are included if the 
		 * host shows up - some columns are always included, e.g.
		 * the info- and trends-columns, but we dont want that to
		 * trigger a host being on the bb2 page!
		 */
		switch (summarytype) {
		  case PAGE_BB2:
			/* Normal BB2 page */
			if (h->hostentry->nobb2 || (bb2nodialups && h->hostentry->dialup)) 
				useit = 0;
			else
				useit = (( (1 << h->hostentry->bb2color) & bb2colors ) != 0);
			break;

		  case PAGE_NK:
			/* The NK page */
			for (useit=0, e=h->hostentry->entries; (e && !useit); e=e->next) {
				if (e->alert && !e->acked) {
					if (e->color == COL_RED) {
						useit = 1;
					}
					else {
						if (!nkonlyreds) {
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
			  case PAGE_BB2:
				if (h->hostentry->bb2color > bb2page.color) bb2page.color = h->hostentry->bb2color;
				break;
			  case PAGE_NK:
				if (h->hostentry->bbnkcolor > bb2page.color) bb2page.color = h->hostentry->bbnkcolor;
				break;
			}

			/* We need to create a copy of the original record, */
			/* as we will diddle with the pointers */
			newhost = (host_t *) calloc(1, sizeof(host_t));
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
	  case PAGE_BB2:
		sprintf(filename, "bb2%s", htmlextension);
		sprintf(rssfilename, "bb2%s", rssextension);
		break;
	  case PAGE_NK:
		sprintf(filename, "bbnk%s", htmlextension);
		sprintf(rssfilename, "bbnk%s", rssextension);
		break;
	}

	sprintf(tmpfilename, "%s.tmp", filename);
	output = fopen(tmpfilename, "w");
	if (output == NULL) {
		errprintf("Cannot create file %s: %s\n", tmpfilename, strerror(errno));
		return bb2page.color;
	}

	if (wantrss) {
		sprintf(tmprssfilename, "%s.tmp", rssfilename);
		rssoutput = fopen(tmprssfilename, "w");
		if (rssoutput == NULL) {
			errprintf("Cannot create RSS file %s: %s\n", tmpfilename, strerror(errno));
			return bb2page.color;
		}
	}

	headfoot(output, hf_prefix[summarytype], "", "header", bb2page.color);
	do_rss_header(rssoutput);

	fprintf(output, "<center>\n");
	fprintf(output, "\n<A NAME=begindata>&nbsp;</A> \n<A NAME=\"hosts-blk\">&nbsp;</A>\n");

	if (bb2page.hosts) {
		do_hosts(bb2page.hosts, 0, NULL, NULL, output, rssoutput, "", summarytype, NULL);
	}
	else {
		/* "All Monitored Systems OK */
		fprintf(output, "<FONT SIZE=+2 FACE=\"Arial, Helvetica\"><BR><BR><I>All Monitored Systems OK</I></FONT><BR><BR>");
	}

	if ((snapshot == 0) && (summarytype == PAGE_BB2)) {
		do_bb2ext(output, "BBMKBB2EXT", "mkbb");

		/* Dont redo the eventlog or acklog things */
		if (bb2eventlog && !havedoneeventlog) {
			do_eventlog(output, bb2eventlogmaxcount, bb2eventlogmaxtime, 
				    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, bb2nodialups, 
				    host_exists,
				    NULL, NULL, NULL, S_NONE);
		}
		if (bb2acklog && !havedoneacklog) do_acklog(output, bb2acklogmaxcount, bb2acklogmaxtime);
	}

	fprintf(output, "</center>\n");
	headfoot(output, hf_prefix[summarytype], "", "footer", bb2page.color);
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

	if (nssidebarfilename) do_netscape_sidebar(nssidebarfilename, bb2page.hosts);

	if (lognkstatus && (summarytype == PAGE_NK)) {
		host_t *hwalk;
		entry_t *ewalk;
		char *msgptr;
		char msgline[MAX_LINE_LEN];
		FILE *nklog;
		char nklogfn[PATH_MAX];
		char svcspace;

		sprintf(nklogfn, "%s/nkstatus.log", xgetenv("BBSERVERLOGS"));
		nklog = fopen(nklogfn, "a");
		if (nklog == NULL) {
			errprintf("Cannot log NK status to %s: %s\n", nklogfn, strerror(errno));
		}

		init_timestamp();
		combo_start();
		init_status(bb2page.color);
		sprintf(msgline, "status %s.%s %s %s NK page %s\n\n", xgetenv("MACHINE"), 
			lognkstatus, colorname(bb2page.color), timestamp, colorname(bb2page.color));
		addtostatus(msgline);

		if (nklog) fprintf(nklog, "%u\t%s", (unsigned int)getcurrenttime(NULL), colorname(bb2page.color));

		for (hwalk = bb2page.hosts; hwalk; hwalk = hwalk->next) {
			msgptr = msgline;
			msgptr += sprintf(msgline, "&%s %s :", colorname(hwalk->color), hwalk->hostname);
			if (nklog) fprintf(nklog, "\t%s ", hwalk->hostname);
			svcspace = '(';

			for (ewalk = hwalk->entries; (ewalk); ewalk = ewalk->next) {
				if ((summarytype == PAGE_BB2) || (ewalk->alert)) {
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

		h1 = bb2page.hosts;
		while (h1) {
			h2 = h1;
			h1 = h1->next;
			xfree(h2);
		}
	}

	return bb2page.color;
}
