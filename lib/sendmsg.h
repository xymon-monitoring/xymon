/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SENDMSG_H_
#define __SENDMSG_H_

#define XYMON_TIMEOUT 15  /* Default timeout for a request going to Xymon server */
#define PAGELEVELSDEFAULT "red purple"

typedef enum {
	XYMONSEND_OK,
	XYMONSEND_EBADIP,
	XYMONSEND_EIPUNKNOWN,
	XYMONSEND_ENOSOCKET,
	XYMONSEND_ECANNOTDONONBLOCK,
	XYMONSEND_ECONNFAILED,
	XYMONSEND_ESELFAILED,
	XYMONSEND_ETIMEOUT,
	XYMONSEND_EWRITEERROR,
	XYMONSEND_EREADERROR,
	XYMONSEND_EBADURL 
} sendresult_t;

typedef struct sendreturn_t {
	FILE *respfd;
	strbuffer_t *respstr;
	int fullresponse;
	int haveseenhttphdrs;
} sendreturn_t;

extern int xymonmsgcount;
extern int xymonstatuscount;
extern int xymonnocombocount;
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

extern void meta_start(void);
extern void meta_end(void);

extern void init_meta(char *metaname);
extern void addtometa(char *p);
extern void finish_meta(void);

#endif

