/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __NETSERVICES_H__
#define __NETSERVICES_H__

/*
 * Flag bits for known TCP services
 */
#define TCP_GET_BANNER 0x0001
#define TCP_TELNET     0x0002
#define TCP_SSL        0x0004
#define TCP_HTTP       0x0008
#define TCP_ALPN       0x0010
/* The service is a multi-step dialogue, not a single send-then-match probe.
   Set by the protocols.cfg parser when the steps cannot be expressed as one
   sendtxt plus one exptext: an expect comes first, or there is more than one
   of either. Single-step services keep the old path untouched. */
#define TCP_DIALOGUE   0x0020

/* One step of a protocol dialogue, in the order protocols.cfg lists them.
   Kept as a list rather than an array because the parser appends while it
   reads and every alias of a service shares the same shape. */
#define STEP_SEND    1
#define STEP_EXPECT  2
#define STEP_LABEL   3	/* a jump target; performs no I/O */
#define STEP_CAPTURE 4	/* pull a value out of the reply just matched */
#define STEP_WHEN    5	/* branch on a captured value */
#define STEP_JUMP    6	/* unconditional; emitted to skip an else-arm */
#define STEP_CREDS   7	/* bind ${username}/${password} from the store */
#define STEP_STARTTLS 8	/* upgrade this connection to TLS, here */
#define STEP_TIMEOUT 9	/* budget for the wait that follows */

/* STEP_LABEL, STEP_CAPTURE, STEP_WHEN, STEP_JUMP and STEP_CREDS touch no
   socket. The driver runs them to completion between I/O steps, so a
   dialogue never sits in a state that has nothing to wait for. */
#define STEP_IS_INSTANT(t) \
	(((t) == STEP_LABEL) || ((t) == STEP_CAPTURE) || ((t) == STEP_WHEN) || \
	 ((t) == STEP_JUMP) || ((t) == STEP_CREDS) || ((t) == STEP_TIMEOUT))

/* What an expect does once its pattern matches. Anything other than
   ACT_NEXT is an edge that leaves the straight line, which is what makes
   this a graph rather than a list. */
#define ACT_NEXT 0
#define ACT_GOTO 1
#define ACT_FAIL 2

/* Consecutive STEP_EXPECTs are alternatives of ONE state: the first whose
   pattern matches wins and its action fires, so a state has as many
   outgoing edges as it has alternatives, plus the implicit failure edge
   taken when every alternative has been ruled out. */
typedef struct svcstep_t {
	int type;
	unsigned char *text;		/* send/expect literal, or capture regex */
	int len;
	int action;			/* ACT_* -- expect steps only */
	char *target;			/* goto label, unresolved */
	struct svcstep_t *targetstep;	/* ... and resolved, after parsing */
	char *label;			/* STEP_LABEL: the name it defines */
	char *varname;			/* STEP_CAPTURE/WHEN: variable name */
	void *re;			/* compiled pcre2_code, or NULL */
	char *user, *pass;		/* STEP_CREDS: resolved at config load */
	unsigned char *until;		/* expect ... until "X": end-of-reply marker */
	int untillen;
	int seconds;			/* STEP_TIMEOUT: budget for the following wait */
	int ambiguous;			/* this alternation group has overlapping patterns */
	int maxaltlen;			/* ... and the longest pattern in it */
	struct svcstep_t *next;
} svcstep_t;

typedef struct svcinfo_t {
	char *svcname;
	unsigned char *sendtxt;
	int  sendlen;
	unsigned char *exptext;
	int  expofs, explen;
	unsigned int flags;
	int port;
	char *alpns;
	svcstep_t *steps;	/* NULL unless TCP_DIALOGUE */
} svcinfo_t;

extern char *init_tcp_services(void);
extern void dump_tcp_services(void);
extern int default_tcp_port(char *svcname);
extern svcinfo_t *find_tcp_service(char *svcname);

#endif

