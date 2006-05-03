/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
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

typedef struct svcinfo_t {
	char *svcname;
	unsigned char *sendtxt;
	int  sendlen;
	unsigned char *exptext;
	int  expofs, explen;
	unsigned int flags;
	int port;
} svcinfo_t;

extern char *init_tcp_services(void);
extern void dump_tcp_services(void);
extern int default_tcp_port(char *svcname);
extern svcinfo_t *find_tcp_service(char *svcname);

#endif

