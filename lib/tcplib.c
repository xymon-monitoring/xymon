/*----------------------------------------------------------------------------*/
/*                                                                            */
/* This file implements a TCP-based communications library, while trying to   */
/* hide all of the messy details.                                             */
/* It supports:                                                               */
/*   - IPv4 connections                                                       */
/*   - IPv6 connections                                                       */
/*   - SSL/TLS encrypted connections                                          */
/*   - server mode (listening for connections)                                */
/*   - client mode (initiates connections)                                    */
/*   - Parallel handling of multiple connections                              */
/*                                                                            */
/* Copyright (C) 2011-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: tcplib.c 7271 2013-08-11 09:43:16Z storner $";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>

#include "config.h"
#include "tcplib.h"


int conn_is_ip(char *ip)
{
	int res = 0;

#ifdef IPV4_SUPPORT
	if (!res) {
		struct in_addr addr;
		res = (inet_pton(AF_INET, ip, &addr) == 1) ? 4 : 0;
	}
#endif

#ifdef IPV6_SUPPORT
	if (!res) {
		struct in6_addr addr;
		res = (inet_pton(AF_INET6, ip, &addr) == 1) ? 6 : 0;
	}
#endif

	return res;
}


int conn_null_ip(char *ip)
{
#ifdef IPV4_SUPPORT
	{
		struct in_addr addr;
		if ((inet_pton(AF_INET, ip, &addr) == 1) && (addr.s_addr == INADDR_ANY)) return 1;
	}
#endif

#ifdef IPV6_SUPPORT
	{
		struct in6_addr addr;
		if ((inet_pton(AF_INET6, ip, &addr) == 1) && (memcmp(&addr.s6_addr, in6addr_any.s6_addr, sizeof(in6addr_any.s6_addr))== 0)) return 1;
	}
#endif

	return 0;
}

