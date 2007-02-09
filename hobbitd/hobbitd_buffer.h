/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HOBBITD_BUFFER_H__
#define __HOBBITD_BUFFER_H__

enum msgchannels_t { C_STATUS=1, C_STACHG, C_PAGE, C_DATA, C_NOTES, C_ENADIS, C_CLIENT, C_CLICHG, C_USER, C_LAST };

extern unsigned int shbufsz(enum msgchannels_t chnid);
#endif

