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

typedef struct sender_t {
	unsigned long int ipval;
	int ipmask;
} sender_t;


extern sender_t *get_ipaccess_list(char *iplist);
extern int ok_ipaccess_sender(sender_t *oklist, char *targetip, char *sender, char *msgbuf);

#endif

