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

#ifndef __BBGEN_H_
#define __BBGEN_H_

#include <time.h>

/* Structure defs for bbgen */

/*
   page_t                                hostlist_t          state_t
     name                                  hostentry --+       hostname
     title                                 next        |       entry --+
     color                                             |       next    |
     subpages                              +-----------+               |
     groups -------> group_t               |                   +-------+
     hosts              title              V                   |
     next               color                                  |
                        hosts ---------> host_t                |
                        next               hostname            |
                                           ip                  |
                                           dialup              |
                      +------------------- link                V
                      |                    color
                      |                    entries ---------> entry_t
                      |                    next                 column -------> col_t
                      |                                         color             name
                      |                                         age            +- link
                      |                                         oldage         |  next
                      |                                         acked          |
                      |                                         next           |
                      |                                                        |
                      |+-------------------------------------------------------+
                      ||
                      VV
                    link_t
                      name
                      filename
                      urlprefix
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

/* Info-link definitions. */
typedef struct {
	char	name[64];
	char	filename[64];
	char	urlprefix[20];	/* "/help", "/notes" etc. */
	void	*next;
} link_t;

/* Column definitions.                     */
/* Basically a list of all possible column */
/* names with links to their help-texts    */
typedef struct {
	char	name[20];
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
	void	*next;
} entry_t;

/* Aux. list definition for loading state of all hosts into one list */
typedef struct {
	char	*hostname;
	entry_t	*entry;
	void	*next;
} state_t;

typedef struct {
	char	hostname[60];
	char	ip[16];
	int	dialup;
	link_t	*link;
	entry_t	*entries;
	int	color;		/* Calculated */
	char	*alerts;
	void	*next;
} host_t;

/* Aux. list definition for quick access to host records */
typedef struct {
	host_t	*hostentry;
	void	*next;
} hostlist_t;

typedef struct {
	char	title[200];
	host_t	*hosts;
	void	*next;
} group_t;


typedef struct {
	char	name[20];
	char	title[200];
	int	color;		/* Calculated */
	void	*next;
	void	*subpages;
	group_t	*groups;
	host_t	*hosts;
} page_t;


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

extern page_t		*pagehead;
extern link_t 		*linkhead, null_link;
extern hostlist_t	*hosthead;
extern state_t		*statehead;
extern col_t		*colhead, null_column;

#endif

