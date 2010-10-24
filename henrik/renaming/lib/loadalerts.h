/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2010 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADALERTS_H__
#define __LOADALERTS_H__

#include <time.h>
#include <stdio.h>

/* The clients probably dont have the pcre headers */
#if defined(LOCALCLIENT) || !defined(CLIENTONLY)
#include <pcre.h>

typedef enum { A_PAGING, A_NORECIP, A_ACKED, A_RECOVERED, A_NOTIFY, A_DEAD } astate_t;

typedef struct activealerts_t {
	/* Identification of the alert */
	char *hostname;
	char *testname;
	char *location;
	char *osname;
	char *classname;
	char *groups;
	char ip[IP_ADDR_STRLEN];

	/* Alert status */
	int color, maxcolor;
	unsigned char *pagemessage;
	unsigned char *ackmessage;
	time_t eventstart;
	time_t nextalerttime;
	astate_t state;
	int cookie;

	struct activealerts_t *next;
} activealerts_t;

/* These are the criteria we use when matching an alert. Used both generally for a rule, and for recipients */
enum method_t { M_MAIL, M_SCRIPT, M_IGNORE };
enum msgformat_t { ALERTFORM_TEXT, ALERTFORM_PLAIN, ALERTFORM_SMS, ALERTFORM_PAGER, ALERTFORM_SCRIPT, ALERTFORM_NONE };
enum recovermsg_t { SR_UNKNOWN, SR_NOTWANTED, SR_WANTED };
typedef struct criteria_t {
	int cfid;
	char *cfline;
	char *pagespec;		/* Pages to include */
	pcre *pagespecre;
	char *expagespec;	/* Pages to exclude */
	pcre *expagespecre;
	char *hostspec;		/* Hosts to include */
	pcre *hostspecre;
	char *exhostspec;	/* Hosts to exclude */
	pcre *exhostspecre;
	char *svcspec;		/* Services to include */
	pcre *svcspecre;
	char *exsvcspec;	/* Services to exclude */
	pcre *exsvcspecre;
	char *classspec;
	pcre *classspecre;
	char *exclassspec;
	pcre *exclassspecre;
	char *groupspec;
	pcre *groupspecre;
	char *exgroupspec;
	pcre *exgroupspecre;
	int colors;
	char *timespec;
	int minduration, maxduration;	/* In seconds */
	enum recovermsg_t sendrecovered, sendnotice;
} criteria_t;

/* This defines a recipient. There may be some criteria, and then how we send alerts to him */
typedef struct recip_t {
	int cfid;
	criteria_t *criteria;
	enum method_t method;
	char *recipient;
	char *scriptname;
	enum msgformat_t format;
	time_t interval;		/* In seconds */
	int stoprule, unmatchedonly, noalerts;
	struct recip_t *next;
} recip_t;

extern int load_alertconfig(char *configfn, int alertcolors, int alertinterval);
extern void dump_alertconfig(int showlinenumbers);

extern int stoprulefound;
extern recip_t *next_recipient(activealerts_t *alert, int *first, int *anymatch, time_t *nexttime);
extern int have_recipient(activealerts_t *alert, int *anymatch);

extern void alert_printmode(int on);
extern void print_alert_recipients(activealerts_t *alert, strbuffer_t *buf);
#endif

#endif

