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
#define TCP_DIALOGUE_BROKEN 0x0040	/* refused when the file was read: do not report OK */
#define TCP_CONNECT_ONLY 0x0080		/* declared unverifiable: the port opening is the whole check */
/* The service is a STATE MACHINE, not a straight line of steps: it declares
   named states and the edges between them, so what runs next depends on what
   the server answered. Set only for entries that say "state", which is why an
   entry written the old way -- every entry in protocols.cfg -- keeps the path
   it has always taken. These come from protocols2.cfg. */
#define TCP_STATEMACHINE 0x0100

/* One step of a protocol dialogue, in the order protocols.cfg lists them.
   Kept as a list rather than an array because the parser appends while it
   reads and every alias of a service shares the same shape. */
#define STEP_SEND   1
#define STEP_EXPECT 2
#define STEP_STARTTLS 3	/* upgrade this connection to TLS, here */
#define STEP_STARTIAC 4	/* negotiate telnet options, here */
#define STEP_LABEL    5	/* names the state that follows; performs no I/O */

/* What an edge does when its expect matches. ACT_NEXT is the straight line
   every protocols.cfg entry takes; the rest exist only for a state machine. */
#define ACT_NEXT    0	/* fall through to the next step */
#define ACT_GOTO    1	/* continue in the state named by "target" */
#define ACT_SUCCESS 2	/* the test ends here, green */
#define ACT_WARNING 3	/* the test ends here, yellow */
#define ACT_FAIL    4	/* the test ends here, red */

typedef struct svcstep_t {
	int type;
	unsigned char *text;
	int len;
	unsigned char *until;		/* expect ... until "X": end-of-reply marker */
	int untillen;
	/*
	 * expect "X" at N: the literal sits at byte N of the reply rather than
	 * at its start. -1 means no offset was given, which 0 cannot say.
	 */
	int ofs;
	/*
	 * send "X" fin: retire the write direction once this send has gone out,
	 * so the peer reads EOF while we keep receiving. Named for the TCP FIN
	 * it sends; the direction is the one "send" implies.
	 */
	int fin;
	int action;			/* ACT_*: what taking this edge does */
	char *target;			/* ACT_GOTO: the state named, before it is resolved */
	struct svcstep_t *targetstep;	/* ... and the STEP_LABEL it resolves to */
	char *label;			/* STEP_LABEL: the name this state answers to */
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
	char *startlabel;	/* TCP_STATEMACHINE: "start NAME", the first state */
} svcinfo_t;

extern char *init_tcp_services(void);
extern void dump_tcp_services(void);
extern int tcp_services_unreadable(void);
extern int default_tcp_port(char *svcname);
extern svcinfo_t *find_tcp_service(char *svcname);

#endif

