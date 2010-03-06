/*----------------------------------------------------------------------------*/
/* Hobbit monitor network test tool.                                          */
/*                                                                            */
/* Copyright (C) 2008-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HTTPCOOKIES_H__
#define __HTTPCOOKIES_H__

typedef struct cookielist_t {
	char *host;
	int  tailmatch;
	char *path;
	int  secure;
	char *name;
	char *value;
	struct cookielist_t *next;
} cookielist_t;

extern cookielist_t *cookiehead;

extern RbtHandle cookietree;
extern void init_session_cookies(char *urlhost, char *cknam, char *ckpath, char *ckval);
extern void update_session_cookies(char *hostname, char *urlhost, char *headers);
extern void save_session_cookies(void);

extern void load_cookies(void);

#endif

