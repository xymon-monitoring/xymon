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
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBGEN_H_
#define __BBGEN_H_

#include <time.h>
#include <stddef.h>

#define VERSION "2.1pre"

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
     oldage   |         onlycols           group               |
     next     |         next               hostname            |
      ^       +------------------------>   ip                  |
      |                                    dialup              |
      +---------------------------------   parent              |
                                           color               |
                                           nopropyellowtests   |
                                           nopropredtests      |
					   rawentry            |
                      +------------------  link                V
                      |                    entries ---------> entry_t
                      |                    oldage               column -------> col_t
                      |                    larrdgraphs          color             name
                      |                    next                 age            +- link
                      |                                         oldage         |  next
                      |                                         acked          |
                      |                                         alert          |
                      |                                         propagate      |
                      |                                         next           |
                      |                                                        |
                      |+-------------------------------------------------------+
                      ||
                      VV
                    link_t
                      name
                      filename
                      urlprefix


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
  test-names are not stored directly, but in the linked "col_t" list.
  "age" is the "Status unchanged in X" text from the logfile. "oldage" is
  a boolean indicating if "age" is more than 1 day. "alert" means this 
  test belongs on the reduced summary (alerts) page.

  state_t is a simple 1-dimensional list of all tests (entry_t records).

  link_t is a simple way of storing all links to /help/ and /notes/ URL's,
  so they can be pre-loaded to avoid doing a lot of file lookups while
  generating the webpages.
  "name" is the text that finds the link (e.g. a pagename, a hostname or
  a testname); "filename" is the filename for the link, and "urlprefix"
  contains "help" or "notes" depending on where the file is located.
*/

#define COL_GREEN	0
#define COL_CLEAR 	1
#define COL_BLUE  	2
#define COL_PURPLE 	3
#define COL_YELLOW	4
#define COL_RED		5

#define PAGE_BB		0
#define PAGE_BB2	1
#define PAGE_NK		2


/* Max length of a single line in bb-hosts */
#define MAX_LINE_LEN 4096

/* Max size of a BB message */
/* NB: This MUST match your MAXSIZE setting in bb.h */
#define MAXMSG 8192

/* Max number of purple messages in one run */
#define MAX_PURPLE_PER_RUN	30

/* Max length of a filename */
#ifndef MAX_PATH
#ifndef MAXPATHLEN
#define MAX_PATH 4096
#else
#define MAX_PATH MAXPATHLEN
#endif
#endif


/* Info-link definitions. */
typedef struct {
	char	*name;
	char	*filename;
	char	*urlprefix;	/* "/help", "/notes" etc. */
	void	*next;
} link_t;

typedef struct {
	char	*rrdname;
	void	*next;
} rrd_t;

/* Column definitions.                     */
/* Basically a list of all possible column */
/* names with links to their help-texts    */
typedef struct {
	char	*name;
	link_t	*link;
	void	*next;
} col_t;

typedef struct {
	col_t	*column;
	void	*next;
} col_list_t;

/* Measurement entry definition               */
/* This points to a column definition, and    */
/* contains the actual color of a measurement */
/* Linked list.                               */
typedef struct {
	col_t	*column;
	int	color;
	char	age[20];
	int	oldage;
	int	acked;
	int	alert;
	int	propagate;
	char 	*sumurl;
	void	*next;
} entry_t;

/* Aux. list definition for loading state of all tests into one list */
typedef struct {
	entry_t	*entry;
	void	*next;
} state_t;

typedef struct {
	char	*hostname;
	char	ip[16];
	int	dialup;
	link_t	*link;
	entry_t	*entries;
	int	color;		/* Calculated */
	char	*alerts;
	char    *nopropyellowtests;
	char    *nopropredtests;
	char    *rawentry;
	rrd_t	*rrds;
	char    *larrdgraphs;
	int     oldage;
	char	*pretitle;
	void	*parent;
	void	*next;
} host_t;

/* Aux. list definition for quick access to host records */
typedef struct {
	host_t	*hostentry;
	void	*next;
} hostlist_t;

typedef struct {
	char	*title;
	char	*onlycols;
	host_t	*hosts;
	char	*pretitle;
	void	*next;
} group_t;


typedef struct {
	char	*name;
	char	*title;
	int	color;		/* Calculated */
	int     oldage;
	char	*pretitle;
	void	*next;
	void	*subpages;
	void	*parent;
	group_t	*groups;
	host_t	*hosts;
} bbgen_page_t;

typedef struct {
	char	*name;
	char	*receiver;
	char	*url;
	void	*next;
} summary_t;

typedef struct {
	char	*row;
	char	*column;
	char	*url;
	int	color;
	void	*next;
} dispsummary_t;

/* Format of records in the $BBHIST/allevents file */
typedef struct {
	char	hostname[60];
	char	service[20];
	time_t	eventtime;
	time_t	changetime;
	time_t	duration;
	int	newcolor;	/* stored as "re", "ye", "gr" etc. */
	int	oldcolor;
	int	state;		/* 2=escalated, 1=recovered, 0=no change */
} event_t;

extern bbgen_page_t	*pagehead;
extern link_t 		*linkhead, null_link;
extern hostlist_t	*hosthead;
extern state_t		*statehead;
extern col_t		*colhead, null_column;
extern summary_t	*sumhead;
extern dispsummary_t	*dispsums;
extern int		bb_color, bb2_color, bbnk_color;

#endif

