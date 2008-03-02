/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SENDMSG_H_
#define __SENDMSG_H_

#define BBTALK_TIMEOUT 15  /* Default timeout for a request going to bbd */
#define PAGELEVELSDEFAULT "red purple"

#define BB_OK			0
#define BB_EBADIP		1
#define BB_EIPUNKNOWN		2
#define BB_ENOSOCKET 		3
#define BB_ECANNOTDONONBLOCK	4
#define BB_ECONNFAILED		5
#define BB_ESELFAILED		6
#define BB_ETIMEOUT		7
#define BB_EWRITEERROR		8
#define BB_EBADURL		9

extern int bbmsgcount;
extern int bbstatuscount;
extern int bbnocombocount;
extern int dontsendmessages;
extern int sendcompressedmessages;

extern void setproxy(char *proxy);
extern int sendmessage(char *msg, char *recipient, FILE *respfd, char **respstr, int fullresponse, int timeout);

extern void combo_start(void);
extern void combo_end(void);

extern void init_status(int color);
extern void addtostatus(char *p);
extern void addtostrstatus(strbuffer_t *p);
extern void finish_status(void);

extern void meta_start(void);
extern void meta_end(void);

extern void init_meta(char *metaname);
extern void addtometa(char *p);
extern void finish_meta(void);

#endif

