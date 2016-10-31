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

#define TIMEOUT_USEENV -2
#define XYMON_TIMEOUT TIMEOUT_USEENV  /* "Default" timeout for a request going to Xymon server */
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
	XYMONSEND_EBADURL,
	XYMONSEND_EBADMSG,
	XYMONSEND_EEMPTY,
	XYMONSEND_EOTHER
} sendresult_t;
extern char *strxymonsendresult(sendresult_t s);


#define RESPONSE_NONE	0	/* No response */
#define RESPONSE_FIRST	1	/* Transmit to first, return the first response */
#define RESPONSE_ALL	2
extern int msgwantsresponse(char *msg);

typedef struct sendreturn_t {
	FILE *respfd;
	strbuffer_t *respstr;
	int fullresponse;
} sendreturn_t;

extern void setproxy(char *proxy);
extern sendresult_t sendmessage_safe(char *msg, size_t msglen, char *recipient, int timeout, sendreturn_t *reponse);

extern sendresult_t sendmessage_buffer(strbuffer_t *msgbuf, char *recipient, int timeout, sendreturn_t *response);
#define sendmessage(A,B,C,D) sendmessage_safe(A, strlen(A), B, C, D)
extern sendreturn_t *newsendreturnbuf(int fullresponse, FILE *respfd);
extern void freesendreturnbuf(sendreturn_t *s);
extern char *getsendreturnstr(sendreturn_t *s, int takeover);

extern void combo_start(void);
extern void combo_end(void);
extern void combo_add(strbuffer_t *msg);
extern void combo_start_local(void);
extern void combo_addchar(char *p);
extern void combo_addcharbytes(char *p, size_t len);

extern int sendmessage_init_local(void);
extern void sendmessage_finish_local(void);
extern sendresult_t sendmessage_local(char *msg, size_t msglen);
extern sendresult_t sendmessage_local_buffer(strbuffer_t *msgbuf);

extern void init_status(int color);
extern void addtostatus(char *p);
extern void addtostrstatus(strbuffer_t *p);
extern void finish_status(void);

#endif

