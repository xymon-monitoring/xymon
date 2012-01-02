/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __NTPTALK_H__
#define __NTPTALK_H__

#include "tcptalk.h"

extern int ntp_callback(tcpconn_t *connection, enum conn_callback_t id, void *userdata);
#endif

