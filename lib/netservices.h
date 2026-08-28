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
#define STEP_SEND   1
#define STEP_EXPECT 2

typedef struct svcstep_t {
	int type;
	unsigned char *text;
	int len;
	unsigned char *until;		/* expect ... until "X": end-of-reply marker */
	int untillen;
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

