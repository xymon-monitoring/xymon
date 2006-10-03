/*----------------------------------------------------------------------------*/
/* Hobbit overview webpage generator tool.                                    */
/*                                                                            */
/* This file holds data-definitions and declarations used in bbgen.           */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBGEN_H_
#define __BBGEN_H_

#include <time.h>
#include "libbbgen.h"

/* Structure defs for bbgen */

/*
   This "drawing" depicts the relations between the various data objects used by bbgen.
   Think of this as doing object-oriented programming in plain C.



   bbgen_page_t                          hostlist_t          state_t
+->  name                                  hostentry --+       entry --+
|    title                                 next        |       next    |
|    color                                             |               |
|    subpages                              +-----------+       +-------+
|    groups -------> group_t               |                   |
|    hosts ---+         title              V                   |
+--- parent   |         hosts ---------> host_t                |
     oldage   |         onlycols           hostname            |
     next     |         next               ip                  |
      ^       +------------------------>   color               |
      |                                    oldage              |
      |                                    bb2color            |
      |                                    bbnkcolor           |
      +---------------------------------   parent              |
                                           alerts              |
                                           waps                |
					   anywaps             |
                                           nopropyellowtests   |
                                           nopropredtests      |
                                           noproppurpletests   |
                                           nopropacktests      |
					   rawentry            V
                                           entries ---------> entry_t
                                           dialup               column -------> bbgen_col_t
                                           reportwarnlevel      color             name
                                           comment              age               next
                                           next                 oldage
                                                                acked
                                                                alert
                                                                onwap
                                                                propagate
                                                                reportinfo
								fileage
                                                                next


  bbgen_page_t structure holds data about one BB page - the first record in this list
  represents the top-level bb.html page. Other pages in the list are defined
  using the bb-hosts "page" directive and access via the page->next link.

  subpages are stored in bbgen_page_t structures also. Accessible via the "subpages"
  link from a page.

  group_t structure holds the data from a "group" directive. groups belong to
  pages or subpages. 
  Currently, all groups act as "group-compress" directive.

  host_t structure holds all data about a given host. "color" is calculated as
  the most critical color of the individual entries belonging to this host.
  Individual tests are connected to the host via the "entries" link.

  hostlist_t is a simple 1-dimensional list of all the hosts, for easier
  traversal of the host list.

  entry_t holds the data for a given test (basically, a file in $BBLOGS).
  test-names are not stored directly, but in the linked "bbgen_col_t" list.
  "age" is the "Status unchanged in X" text from the logfile. "oldage" is
  a boolean indicating if "age" is more than 1 day. "alert" means this 
  test belongs on the reduced summary (alerts) page.

  state_t is a simple 1-dimensional list of all tests (entry_t records).
*/

#define PAGE_BB		0
#define PAGE_BB2	1
#define PAGE_NK		2


/* Max number of purple messages in one run */
#define MAX_PURPLE_PER_RUN	30

/* Column definitions.                     */
/* Basically a list of all possible column */
/* names                                   */
typedef struct bbgen_col_t {
	char	*name;
	char	*listname;	/* The ",NAME," string used for searches */
	struct bbgen_col_t	*next;
} bbgen_col_t;

typedef struct col_list_t {
	struct bbgen_col_t	*column;
	struct col_list_t	*next;
} col_list_t;

/* Measurement entry definition               */
/* This points to a column definition, and    */
/* contains the actual color of a measurement */
/* Linked list.                               */
typedef struct entry_t {
	struct bbgen_col_t *column;
	int	color;
	char	age[20];
	int	oldage;
	time_t  fileage;
	int	acked;
	int	alert;
	int	onwap;
	int	propagate;
	char 	*sumurl;
	char	*skin;
	char	*testflags;
	struct reportinfo_t *repinfo;
	struct replog_t *causes;
	char    *histlogname;
	char    *shorttext;
	struct entry_t	*next;
} entry_t;

/* Aux. list definition for loading state of all tests into one list */
typedef struct state_t {
	struct entry_t	*entry;
	struct state_t	*next;
} state_t;

/* OSX has a built-in "host_t" type. */
#define host_t bbgen_host_t

typedef struct host_t {
	char	*hostname;
	char	*displayname;
	char    *clientalias;
	char	*comment;
	char    *description;
	char	ip[IP_ADDR_STRLEN];
	int	dialup;
	struct entry_t	*entries;
	int	color;		/* Calculated */
	int	bb2color;	/* Calculated */
	int	bbnkcolor;	/* Calculated */
	int     oldage;
	char	*alerts;
	int	nktime;
	int	anywaps;
	int	wapcolor;
	char	*waps;
	char    *nopropyellowtests;
	char    *nopropredtests;
	char    *noproppurpletests;
	char    *nopropacktests;
	char	*pretitle;
	struct bbgen_page_t *parent;
	double  reportwarnlevel;
	char	*reporttime;
	int     nobb2;
	struct host_t	*next;
} host_t;

/* Aux. list definition for quick access to host records */
typedef struct hostlist_t {
	struct host_t		*hostentry;
	struct hostlist_t    	*clones;
} hostlist_t;

typedef struct group_t {
	char	*title;
	char	*onlycols;
	char	*exceptcols;
	struct host_t	*hosts;
	char	*pretitle;
	struct group_t	*next;
} group_t;


typedef struct bbgen_page_t {
	char	*name;
	char	*title;
	int	color;		/* Calculated */
	int     oldage;
	char	*pretitle;
	struct bbgen_page_t	*next;
	struct bbgen_page_t	*subpages;
	struct bbgen_page_t	*parent;
	struct group_t	*groups;
	struct host_t	*hosts;
} bbgen_page_t;

typedef struct summary_t {
	char		*name;
	char		*receiver;
	char		*url;
	struct summary_t	*next;
} summary_t;

typedef struct dispsummary_t {
	char		*row;
	char		*column;
	char		*url;
	int		color;
	struct dispsummary_t	*next;
} dispsummary_t;

extern bbgen_page_t	*pagehead;
extern state_t		*statehead;
extern bbgen_col_t	*colhead, null_column;
extern summary_t	*sumhead;
extern dispsummary_t	*dispsums;
extern int		bb_color, bb2_color, bbnk_color;
extern time_t		reportstart, reportend;
extern double           reportwarnlevel, reportgreenlevel;
extern int		reportstyle;
extern int		dynamicreport;
extern int              fqdn;

#endif

