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
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU Public License (GPL), version 2.    */
/* See the file "COPYING" for details.                                        */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <wait.h>

#include "bbgen.h"
#include "util.h"
#include "loaddata.h"
#include "pagegen.h"

int interesting_column(int pagetype, int color, int alert, char *columnname)
{
	switch (pagetype) {
	  case PAGE_BB:
		return 1;
		break;

	  case PAGE_BB2:
		return ((color == COL_RED) || 
			(color == COL_YELLOW) ||
			(color == COL_PURPLE));

	  case PAGE_NK:
		if (alert) {
			if (color == COL_RED) {
				return 1;
			}
			else if ((color == COL_YELLOW) &&
			    (strcmp(columnname, "conn") != 0)) {
				return 1;
			}
		}
		break;
	}

	return 0;
}

col_list_t *gen_column_list(host_t *hostlist, int pagetype)
{
	/*
	 * Build a list of the columns that are in use by
	 * hosts in the hostlist passed as parameter.
	 * The column list must be sorted by column name.
	 */

	/* Meaning of pagetype:
	     0: Normal pages, include all
	     1: bb2.html, all non-green
	     2: bbnk.html, only alert columns
	 */

	col_list_t	*head;
	host_t		*h;
	entry_t		*e;
	col_list_t	*newlistitem, *collist_walk;

	/* Code de-obfuscation trick: Add a null record as the head item */
	/* Simplifies handling since head != NULL and we never have to insert at head of list */
	head = malloc(sizeof(col_list_t));
	head->column = &null_column;
	head->next = NULL;

	for (h = hostlist; (h); h = h->next) {
		for (e = h->entries; (e); e = e->next) {
			if (interesting_column(pagetype, e->color, e->alert, e->column->name)) {
				/* See where e->column should go in list */
				collist_walk = head; 
				while ( (collist_walk->next && 
                               		strcmp(e->column->name, ((col_list_t *)(collist_walk->next))->column->name) > 0) ) {
					collist_walk = collist_walk->next;
				}

				if ((collist_walk->next == NULL) || ((col_list_t *)(collist_walk->next))->column != e->column) {
					/* collist_walk points to the entry before the new one */
					newlistitem = malloc(sizeof(col_list_t));
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

void do_hosts(host_t *head, FILE *output, char *grouptitle, int pagetype)
{
	host_t	*h;
	entry_t	*e;
	col_list_t *groupcols, *gc;
	int	genstatic;
	int	columncount;

	if (head == NULL)
		return;

	genstatic = ( (strcmp(getenv("BBLOGSTATUS"), "STATIC") == 0) ? 1 : 0);

	fprintf(output, "<A NAME=hosts-blk>&nbsp;</A>\n\n");

	groupcols = gen_column_list(head, pagetype);
	if (groupcols) {
		fprintf(output, "<TABLE SUMMARY=\"Group Block\" BORDER=0> \n <TR><TD VALIGN=MIDDLE ROWSPAN=2 CELLPADDING=2><CENTER><FONT %s>%s</FONT></CENTER></TD>\n", getenv("MKBBTITLE"), grouptitle);

		columncount = 1; /* Count the title also */
		for (gc=groupcols; (gc); gc = gc->next, columncount++) {
			fprintf(output, " <TD ALIGN=CENTER VALIGN=BOTTOM WIDTH=45>\n");
			fprintf(output, " <A HREF=\"%s/%s\"><FONT %s><B>%s</B></FONT></A> </TD>\n", 
				getenv("BBWEB"), columnlink(gc->column->link, gc->column->name), 
				getenv("MKBBCOLFONT"), gc->column->name);
		}
		fprintf(output, "</TR> \n<TR><TD COLSPAN=%d><HR WIDTH=100%%></TD></TR>\n\n", columncount);

		for (h = head; (h); h = h->next) {
			fprintf(output, "<TR>\n <TD NOWRAP><A NAME=\"%s\">\n", h->hostname);

			if (h->link != &null_link) {
				fprintf(output, "<A HREF=\"%s/%s\" TARGET=\"_blank\"><FONT %s>%s</FONT></A>\n </TD>",
					getenv("BBWEB"), hostlink(h->link), 
					getenv("MKBBROWFONT"), h->hostname);
			}
			else {
				fprintf(output, "<FONT %s>%s</FONT>\n </TD>",
					getenv("MKBBROWFONT"), h->hostname);
			}

			for (gc = groupcols; (gc); gc = gc->next) {
				fprintf(output, "<TD ALIGN=CENTER>");

				for (e = h->entries; (e && (e->column != gc->column)); e = e->next) ;
				if (e == NULL) {
					fprintf(output, "-");
				}
				else {
					if (e->sumurl) {
						fprintf(output, "<A HREF=\"%s\">", e->sumurl);
					}
					else if (genstatic) {
						fprintf(output, "<A HREF=\"%s/html/%s.%s.html\">",
							getenv("BBWEB"), h->hostname, e->column->name);
					}
					else {
						fprintf(output, "<A HREF=\"%s/bb-hostsvc.sh?HOSTSVC=%s.%s\">",
							getenv("CGIBINURL"), commafy(h->hostname), e->column->name);
					}

					fprintf(output, "<IMG SRC=\"%s/%s\" ALT=\"%s\" HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0></A>",
						getenv("BBSKIN"), dotgiffilename(e),
						alttag(e),
						getenv("DOTHEIGHT"), getenv("DOTWIDTH"));
				}
				fprintf(output, "</TD>\n");
			}

			fprintf(output, "</TR>\n\n");
		}

		fprintf(output, "</TABLE><BR><BR>\n");
	}

	while (groupcols) {
		gc = groupcols;
		groupcols = groupcols->next;
		free(gc);
	}
}

void do_groups(group_t *head, FILE *output)
{
	group_t *g;

	if (head == NULL)
		return;

	fprintf(output, "<CENTER> \n\n<A NAME=begindata>&nbsp;</A>\n");

	for (g = head; (g); g = g->next) {
		do_hosts(g->hosts, output, g->title, PAGE_BB);
	}
	fprintf(output, "\n</CENTER>\n");
}

void do_summaries(dispsummary_t *sums, FILE *output)
{
	dispsummary_t *s;
	host_t *sumhosts = NULL;
	host_t *walk;

	for (s=sums; (s); s = s->next) {
		/* Generate host records out of all unique s->row values */

		host_t *newhost;
		entry_t *newentry;
		dispsummary_t *s2;

		/* Do we already have it ? */
		for (newhost = sumhosts; (newhost && (strcmp(s->row, newhost->hostname) != 0) ); newhost = newhost->next);

		if (newhost == NULL) {
			/* New summary "host" */

			newhost = malloc(sizeof(host_t));
			strcpy(newhost->hostname, s->row);
			strcpy(newhost->ip, "");
			newhost->dialup = 0;
			newhost->color = -1;
			newhost->link = &null_link;
			newhost->entries = NULL;
			newhost->next = NULL;

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
					newentry = malloc(sizeof(entry_t));

					newentry->column = find_or_create_column(s2->column);
					newentry->color = s2->color;
					strcpy(newentry->age, "");
					newentry->oldage = 1; /* Use standard gifs */
					newentry->acked = 0;
					newentry->alert = 0;
					newentry->sumurl = s2->url;
					newentry->next = newhost->entries;
					newhost->entries = newentry;
				}
			}
		}
	}

	fprintf(output, "<A NAME=\"summaries-blk\">\n");
	fprintf(output, "<CENTER>\n");
	fprintf(output, "<TABLE SUMMARY=\"Summary Block\" BORDER=0><TR><TD>\n");
	fprintf(output, "<CENTER><FONT %s>\n", getenv("MKBBTITLE"));
	fprintf(output, "%s\n", getenv("MKBBREMOTE"));
	fprintf(output, "</FONT></CENTER></TD></TR><TR><TD>\n");
	fprintf(output, "<HR WIDTH=100%%></TD></TR>\n");
	fprintf(output, "<TR><TD>\n");

	do_hosts(sumhosts, output, "", 0);

	fprintf(output, "</TD></TR></TABLE>\n");
	fprintf(output, "</CENTER>\n");
}

void do_bb_page(page_t *page, dispsummary_t *sums, char *filename)
{
	FILE	*output;
	page_t	*p;
	link_t  *link;

	output = fopen(filename, "w");
	if (output == NULL) {
		printf("Cannot open file %s\n", filename);
		return;
	}

	headfoot(output, "bb", "", "", "header", page->color);

	fprintf(output, "<BR>\n<CENTER>\n");

	if (page->next) {
		fprintf(output, "<A NAME=\"pages-blk\">\n");
		fprintf(output, "<TABLE SUMMARY=\"Page Block\" BORDER=0>\n");

		fprintf(output, "<TR><TD COLSPAN=2><CENTER> \n<FONT %s>\n", getenv("MKBBTITLE"));
		fprintf(output, "   %s\n", getenv("MKBBLOCAL"));
		fprintf(output, "</FONT></CENTER></TD></TR>\n");
		fprintf(output, "<TR><TD COLSPAN=2><HR WIDTH=100%%></TD></TR>\n");

		for (p = page->next; (p); p = p->next) {

			link = find_link(p->name);
			if (link != &null_link) {
				fprintf(output, "<TR><TD><FONT %s><A HREF=\"%s/%s\">%s</A></FONT></TD>\n", 
					getenv("MKBBROWFONT"),
					getenv("BBWEB"), hostlink(link), 
					p->title);
			}
			else {
				fprintf(output, "<TR><TD><FONT %s>%s</FONT></TD>\n", getenv("MKBBROWFONT"), p->title);
			}

			fprintf(output, "<TD><CENTER><A HREF=\"%s/%s/%s.html\">\n", getenv("BBWEB"), p->name, p->name);
			fprintf(output, "<IMG SRC=\"%s/%s.gif\" WIDTH=\"%s\" HEIGHT=\"%s\" BORDER=0 ALT=\"%s\"></A>\n", 
				getenv("BBSKIN"), colorname(p->color), 
				getenv("DOTWIDTH"), getenv("DOTHEIGHT"),
				colorname(p->color));
			fprintf(output, "</CENTER></TD></TR>\n");
		}

		fprintf(output, "</TABLE><BR><BR>\n");
		fprintf(output, "</CENTER>\n");
	}

	do_hosts(page->hosts, output, "", PAGE_BB);
	do_groups(page->groups, output);
	do_summaries(dispsums, output);

	headfoot(output, "bb", "", "", "footer", page->color);

	fclose(output);
}


void do_page(page_t *page, char *filename, char *upperpagename)
{
	FILE	*output;
	page_t	*p;
	link_t  *link;

	output = fopen(filename, "w");
	if (output == NULL) {
		printf("Cannot open file %s\n", filename);
		return;
	}

	headfoot(output, "bb", page->name, "", "header", page->color);

	fprintf(output, "<BR>\n<CENTER>\n");

	if (page->subpages) {
		fprintf(output, "<A NAME=\"pages-blk\">\n");
		fprintf(output, "<TABLE SUMMARY=\"Page Block\" BORDER=0>\n");

		fprintf(output, "<TR><TD COLSPAN=2><CENTER> \n<FONT %s>\n", getenv("MKBBTITLE"));
		fprintf(output, "   %s\n", getenv("MKBBSUBLOCAL"));
		fprintf(output, "</FONT></CENTER></TD></TR>\n");
		fprintf(output, "<TR><TD COLSPAN=2><HR WIDTH=100%%></TD></TR>");

		for (p = page->subpages; (p); p = p->next) {

			link = find_link(p->name);
			if (link != &null_link) {
				fprintf(output, "<TR><TD><FONT %s><A HREF=\"%s/%s\">%s</A></FONT></TD>\n", 
					getenv("MKBBROWFONT"),
					getenv("BBWEB"), hostlink(link), 
					p->title);
			}
			else {
				fprintf(output, "<TR><TD><FONT %s>%s</FONT></TD>\n", getenv("MKBBROWFONT"), p->title);
			}

			fprintf(output, "<TD><CENTER><A HREF=\"%s/%s/%s/%s.html\">\n", getenv("BBWEB"), upperpagename, p->name, p->name);
			fprintf(output, "<IMG SRC=\"%s/%s.gif\" WIDTH=\"%s\" HEIGHT=\"%s\" BORDER=0 ALT=\"%s\"></A>\n", 
				getenv("BBSKIN"), colorname(p->color), 
				getenv("DOTWIDTH"), getenv("DOTHEIGHT"),
				colorname(p->color));
			fprintf(output, "</CENTER></TD></TR>\n");
		}

		fprintf(output, "</TABLE><BR><BR>\n");
		fprintf(output, "</CENTER>\n");
	}

	do_hosts(page->hosts, output, "", PAGE_BB);
	do_groups(page->groups, output);

	headfoot(output, "bb", page->name, "", "footer", page->color);

	fclose(output);
}

void do_subpage(page_t *page, char *filename, char *upperpagename)
{
	FILE	*output;

	output = fopen(filename, "w");
	if (output == NULL) {
		printf("Cannot open file %s\n", filename);
		return;
	}

	headfoot(output, "bb", upperpagename, page->name, "header", page->color);

	do_hosts(page->hosts, output, "", PAGE_BB);
	do_groups(page->groups, output);

	headfoot(output, "bb", upperpagename, page->name, "footer", page->color);

	fclose(output);
}


void do_eventlog(FILE *output, int maxcount, int maxminutes)
{
	FILE *eventlog;
	char eventlogfilename[256];
	char newcol[3], oldcol[3];
	time_t cutoff;
	event_t	*events;
	int num, eventintime_count;
	struct stat st;
	char l[200];
	char title[200];


	cutoff = ( (maxminutes) ? (time(NULL) - maxminutes*60) : 0);
	if ((!maxcount) || (maxcount > 100)) maxcount = 100;

	sprintf(eventlogfilename, "%s/allevents", getenv("BBHIST"));
	eventlog = fopen(eventlogfilename, "r");
	if (!eventlog) {
		perror("Cannot open eventlog");
		return;
	}

	/* HACK ALERT! */
	if (stat(eventlogfilename, &st) == 0) {
		char dummy[80];
		long curofs;

		/* Assume a log entry is max 80 bytes */
		fseek(eventlog, -80*maxcount, SEEK_END);
		curofs = ftell(eventlog);
		fgets(dummy, sizeof(dummy), eventlog);
	}
	
	events = malloc(maxcount*sizeof(event_t));
	eventintime_count = num = 0;

	while (fgets(l, sizeof(l), eventlog)) {

		sscanf(l, "%s %s %lu %lu %lu %s %s %d",
			events[num].hostname, events[num].service,
			&events[num].eventtime, &events[num].changetime, &events[num].duration, 
			newcol, oldcol, &events[num].state);

		if (events[num].eventtime > cutoff) {
			events[num].newcolor = eventcolor(newcol);
			events[num].oldcolor = eventcolor(oldcol);
			eventintime_count++;

			num = (num + 1) % maxcount;
		}
	}

	if (eventintime_count > 0) {
		int firstevent, lastevent;
		char *bgcolors[2] = { "000000", "000033" };
		int  bgcolor = 0;

		if (eventintime_count <= maxcount) {
			firstevent = 0;
			lastevent = eventintime_count-1;
		}
		else {
			firstevent = num;
			lastevent = ( (num == 0) ? maxcount : (num-1));
			eventintime_count = maxcount;
		}

		sprintf(title, "%d events received in the past %lu minutes",
			eventintime_count, ((time(NULL)-events[firstevent].eventtime) / 60));

		fprintf(output, "<BR><BR>\n");
        	fprintf(output, "<TABLE SUMMARY=\"$EVENTSTITLE\" BORDER=0>\n");
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD></TR>\n", title);

		for (num = lastevent; (num != firstevent); num = ((num == 0) ? (maxcount-1) : (num - 1)) ) {
			fprintf(output, "<TR BGCOLOR=%s>\n", bgcolors[bgcolor]);
			bgcolor = ((bgcolor + 1) % 2);

			fprintf(output, "<TD ALIGN=CENTER>%s</TD>\n", ctime(&events[num].eventtime));

			if (events[num].newcolor == COL_CLEAR) {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=black><FONT COLOR=white>%s</FONT></TD>\n",
					events[num].hostname);
			}
			else {
				fprintf(output, "<TD ALIGN=CENTER BGCOLOR=%s><FONT COLOR=black>%s</FONT></TD>\n",
					colorname(events[num].newcolor),
					events[num].hostname);
			}

			fprintf(output, "<TD ALIGN=LEFT>%s</TD>\n", events[num].service);
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(events[num].hostname, events[num].service, events[num].changetime));
			fprintf(output, "<IMG SRC=\"%s/%s.gif\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=%s></A>\n", 
				getenv("BBSKIN"), colorname(events[num].oldcolor), 
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"), 
				colorname(events[num].oldcolor));
			fprintf(output, "<IMG SRC=\"%s/arrow.gif\" BORDER=0 ALT=\"From -&gt; To\">\n", 
				getenv("BBSKIN"));
			fprintf(output, "<TD><A HREF=\"%s\">\n", 
				histlogurl(events[num].hostname, events[num].service, events[num].eventtime));
			fprintf(output, "<IMG SRC=\"%s/%s.gif\"  HEIGHT=\"%s\" WIDTH=\"%s\" BORDER=0 ALT=%s></A>\n", 
				getenv("BBSKIN"), colorname(events[num].newcolor), 
				getenv("DOTHEIGHT"), getenv("DOTWIDTH"), 
				colorname(events[num].newcolor));
		}

		fprintf(output, "</TABLE>\n");
	}
	else {
		/* No events during the past maxminutes */
		sprintf(title, "No events received in the last %d minutes", maxminutes);

		fprintf(output, "<CENTER><BR>\n");
		fprintf(output, "<TABLE SUMMARY=\"%s\" BORDER=0>\n", title);
		fprintf(output, "<TR BGCOLOR=\"333333\">\n");
		fprintf(output, "<TD ALIGN=CENTER COLSPAN=6><FONT SIZE=-1 COLOR=\"teal\">%s</FONT></TD>\n", title);
		fprintf(output, "</TR>\n");
		fprintf(output, "</TABLE>\n");
		fprintf(output, "</CENTER>\n");
	}

	free(events);
	fclose(eventlog);
}

void do_bb2_page(char *filename, int summarytype)
{
	FILE	*output;
	page_t	bb2page;
	hostlist_t *h;
	entry_t	*e;
	int	useit;
	char    *hf_prefix[2] = { "bb2", "bbnk" };

	/* Build a "page" with the hosts that should be included in bb2 page */
	strcpy(bb2page.name, "");
	strcpy(bb2page.title, "");
	bb2page.color = COL_GREEN;
	bb2page.subpages = NULL;
	bb2page.groups = NULL;
	bb2page.hosts = NULL;
	bb2page.next = NULL;

	for (h=hosthead; (h); h=h->next) {
		switch (summarytype) {
		  case 0:
			/* Normal BB2 page */
			useit = ((h->hostentry->color == COL_RED) || 
				 (h->hostentry->color == COL_YELLOW) || 
				 (h->hostentry->color == COL_PURPLE));
			break;

		  case 1:
			for (useit=0, e=h->hostentry->entries; (e && !useit); e=e->next) {
				useit = useit || (e->alert && ((e->color == COL_RED) || ((e->color == COL_YELLOW) && (strcmp(e->column->name, "conn") != 0))));
			}
			break;
			/* NK page */
		}
		if (useit) {
			host_t *newhost, *walk;

			if (h->hostentry->color > bb2page.color) bb2page.color = h->hostentry->color;

			/* We need to create a copy of the original record, */
			/* as we will diddle with the pointers */
			newhost = malloc(sizeof(host_t));
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

				/* "walk" points to element before the new item */
				newhost->next = walk->next;
				walk->next = newhost;
			}
		}
	}

	output = fopen(filename, "w");
	if (output == NULL) {
		perror("Cannot open file");
		exit(1);
	}

	headfoot(output, hf_prefix[summarytype], "", "", "header", bb2page.color);

	fprintf(output, "<center>\n");
	fprintf(output, "\n<A NAME=begindata>&nbsp;</A> \n<A NAME=\"hosts-blk\">&nbsp;</A>\n");

	if (bb2page.hosts) {
		do_hosts(bb2page.hosts, output, "", (1+summarytype));
	}
	else {
		/* "All Monitored Systems OK */
		fprintf(output, "<FONT SIZE=+2 FACE=\"Arial, Helvetica\"><BR><BR><I>All Monitored Systems OK</I></FONT><BR><BR>");
	}

	if (summarytype == 0) {
		do_eventlog(output, 0, 240);
	}

	fprintf(output, "</center>\n");
	headfoot(output, hf_prefix[summarytype], "", "", "footer", bb2page.color);

	fclose(output);

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
}
