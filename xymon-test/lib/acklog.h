/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef _ACKLOG_H_
#define _ACKLOG_H_

/* Format of records in $BBACKS/acklog file (TAB separated) */
typedef struct ack_t {
	time_t	acktime;
	int	acknum;
	int	duration;	/* Minutes */
	int	acknum2;
	char	*ackedby;
	char	*hostname;
	char	*testname;
	int	color;
	char	*ackmsg;
	int	ackvalid;
} ack_t;

extern int havedoneacklog;
extern void do_acklog(FILE *output, int maxcount, int maxminutes);

#endif
