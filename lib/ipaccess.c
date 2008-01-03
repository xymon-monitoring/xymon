/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module for Hobbit, responsible for loading the host-,    */
/* page-, and column-links defined in the BB directory structure.             */
/*                                                                            */
/* Copyright (C) 2004-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: ipaccess.c,v 1.4 2008-01-03 09:59:13 henrik Exp $";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "libbbgen.h"

sender_t *getsenderlist(char *iplist)
{
	char *p, *tok;
	sender_t *result;
	int count;

	dbgprintf("-> getsenderlist\n");

	count = 0; p = iplist; do { count++; p = strchr(p, ','); if (p) p++; } while (p);
	result = (sender_t *) calloc(1, sizeof(sender_t) * (count+1));

	tok = strtok(iplist, ","); count = 0;
	while (tok) {
		int bits = 32;

		p = strchr(tok, '/');
		if (p) *p = '\0';
		result[count].ipval = ntohl(inet_addr(tok));
		if (p) { *p = '/'; p++; bits = atoi(p); }
		if (bits < 32) 
			result[count].ipmask = (0xFFFFFFFF << (32 - atoi(p)));
		else
			result[count].ipmask = 0xFFFFFFFF;

		tok = strtok(NULL, ",");
		count++;
	}

	dbgprintf("<- getsenderlist\n");

	return result;
}

int oksender(sender_t *oklist, char *targetip, struct in_addr sender, char *msgbuf)
{
	int i;
	unsigned long int tg_ip;
	char *eoln = NULL;

	dbgprintf("-> oksender\n");

	/* If oklist is empty, we're not doing any access checks - so return OK */
	if (oklist == NULL) {
		dbgprintf("<- oksender(1-a)\n");
		return 1;
	}

	/* If we know the target, it would be ok for the host to report on itself. */
	if (targetip) {
		if (strcmp(targetip, "0.0.0.0") == 0) return 1; /* DHCP hosts can report from any address */
		tg_ip = ntohl(inet_addr(targetip));
		if (ntohl(sender.s_addr) == tg_ip) {
			dbgprintf("<- oksender(1-b)\n");
			return 1;
		}
	}

	/* It's someone else reporting about the host. Check the access list */
	i = 0;
	do {
		if ((oklist[i].ipval & oklist[i].ipmask) == (ntohl(sender.s_addr) & oklist[i].ipmask)) {
			dbgprintf("<- oksender(1-c)\n");
			return 1;
		}
		i++;
	} while (oklist[i].ipval != 0);

	/* Refuse and log the message */
	if (msgbuf) { eoln = strchr(msgbuf, '\n'); if (eoln) *eoln = '\0'; }
	errprintf("Refused message from %s: %s\n", inet_ntoa(sender), (msgbuf ? msgbuf : ""));
	if (msgbuf && eoln) *eoln = '\n';

	dbgprintf("<- oksender(0)\n");

	return 0;
}

