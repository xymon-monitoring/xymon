/*----------------------------------------------------------------------------*/
/* bbgen tool                                                                 */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SENDMSG_H_
#define __SENDMSG_H_

#define BBDPORTNUMBER 1984
#define PAGELEVELSDEFAULT "red purple"

extern int bbmsgcount;
extern int bbstatuscount;
extern int bbnocombocount;

extern int sendstatus(char *bbdisp, char *msg);
extern void sendmessage(char *msg, char *recipient);

extern void combo_start(void);
extern void combo_end(void);

extern void init_status(int color);
extern void addtostatus(char *p);
extern void finish_status(void);

#endif

