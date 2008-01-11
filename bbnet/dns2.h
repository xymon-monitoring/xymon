/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DNS2_H__
#define __DNS2_H__

typedef struct dns_resp_t {
	int msgstatus;
	strbuffer_t *msgbuf;
	struct dns_resp_t *next;
} dns_resp_t;

#if (ARES_VERSION_MAJOR > 1)
#error "Unsupported C-ARES version"
#else
#if (ARES_VERSION_MINOR > 4)
extern void dns_detail_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen);
#else
extern void dns_detail_callback(void *arg, int status, unsigned char *abuf, int alen);
#endif
#endif

extern int dns_name_type(char *name);

#endif

