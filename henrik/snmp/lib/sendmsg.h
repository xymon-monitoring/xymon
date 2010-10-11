/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SENDMSG_H_
#define __SENDMSG_H_

#define BBTALK_TIMEOUT 15  /* Default timeout for a request going to bbd */
#define PAGELEVELSDEFAULT "red purple"

typedef enum {
	BB_OK,
	BB_EBADIP,
	BB_EIPUNKNOWN,
	BB_ENOSOCKET,
	BB_ECANNOTDONONBLOCK,
	BB_ECONNFAILED,
	BB_ESELFAILED,
	BB_ETIMEOUT,
	BB_EWRITEERROR,
	BB_EREADERROR,
	BB_EBADURL 
} sendresult_t;

typedef struct sendreturn_t {
	FILE *respfd;
	strbuffer_t *respstr;
	int fullresponse;
	int haveseenhttphdrs;
} sendreturn_t;

extern int bbmsgcount;
extern int bbstatuscount;
extern int bbnocombocount;
extern int dontsendmessages;

extern void setproxy(char *proxy);
extern sendresult_t sendmessage(char *msg, char *recipient, int timeout, sendreturn_t *reponse);
extern sendreturn_t *newsendreturnbuf(int fullresponse, FILE *respfd);
extern void freesendreturnbuf(sendreturn_t *s);
extern char *getsendreturnstr(sendreturn_t *s, int takeover);

extern void combo_start(void);
extern void combo_end(void);

extern void init_status(int color);
extern void addtostatus(char *p);
extern void addtostrstatus(strbuffer_t *p);
extern void finish_status(void);
extern char *copy_status(void);

extern void meta_start(void);
extern void meta_end(void);

extern void init_meta(char *metaname);
extern void addtometa(char *p);
extern void finish_meta(void);

#endif

