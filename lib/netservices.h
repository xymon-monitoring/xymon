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

typedef struct netdialog_t {
	char *name;
	int portnumber;
	char **dialog;
	int option_telnet:1;
	int option_ntp:1;
	int option_dns:1;
	int option_ssl:1;
	int option_starttls:1;
	int option_udp:1;
	int option_external:1;
} netdialog_t;


extern char *silentdialog[];

extern void load_protocols(char *fn);
extern char *init_net_services(void);
extern netdialog_t *find_net_service(char *servicename);

#endif

