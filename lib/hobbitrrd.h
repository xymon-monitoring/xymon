/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __BBLARRD_H__
#define __BBLARRD_H__

typedef struct {
   char *bbsvcname;
   char *larrdsvcname;
   char *larrdpartname;
} larrdsvc_t;

extern larrdsvc_t *find_larrd(char *service, char *flags);

#endif

