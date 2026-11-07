/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __IPACCESS_H__
#define __IPACCESS_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>         /* Someday I'll move to GNU Autoconf for this ... */
#endif

/* One entry of a sender list: an IPv4 or IPv6 network. family 0 ends a list. */
typedef struct sender_t {
	int family;			/* AF_INET or AF_INET6 */
	unsigned char addr[16];		/* network order; IPv4 uses the first 4 bytes */
	int bits;			/* prefix length */
} sender_t;


extern sender_t *getsenderlist(char *iplist);
extern int sender_in_list(sender_t *list, struct sockaddr *sa);
extern int oksender_addr(sender_t *oklist, char *targetip, struct sockaddr *sa, char *msgbuf);
extern int oksender(sender_t *oklist, char *targetip, struct in_addr sender, char *msgbuf);
extern char *sockaddr_text(struct sockaddr *sa, char *buf, size_t buflen);
extern int text_sockaddr(char *text, struct sockaddr_storage *ss);

#endif

