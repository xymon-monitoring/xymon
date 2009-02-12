/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This module contains a shared routine to find the size of a shared memory  */
/* buffer used for one of the Hobbit communications-channels.                 */
/*                                                                            */
/* Copyright (C) 2004-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>
#include <stdlib.h>

#include "libbbgen.h"
#include "hobbitd_buffer.h"

unsigned int shbufsz(enum msgchannels_t chnid)
{
	unsigned int defvalue = 0, result = 0;
	char *v = NULL;

	if (chnid != C_LAST) {
		switch (chnid) {
		  case C_STATUS: v = getenv("MAXMSG_STATUS"); defvalue = 256; break;
		  case C_CLIENT: v = getenv("MAXMSG_CLIENT"); defvalue = 512; break;
		  case C_CLICHG: v = getenv("MAXMSG_CLICHG"); defvalue = shbufsz(C_CLIENT); break;
		  case C_DATA:   v = getenv("MAXMSG_DATA");   defvalue = 256; break;
		  case C_NOTES:  v = getenv("MAXMSG_NOTES");  defvalue = 256; break;
		  case C_STACHG: v = getenv("MAXMSG_STACHG"); defvalue = shbufsz(C_STATUS); break;
		  case C_PAGE:   v = getenv("MAXMSG_PAGE");   defvalue = shbufsz(C_STATUS); break;
		  case C_ENADIS: v = getenv("MAXMSG_ENADIS"); defvalue =  32; break;
		  case C_USER:   v = getenv("MAXMSG_USER");   defvalue = 128; break;
		  default: break;
		}

		if (v) {
			result = atoi(v);
			/* See if it is an old setting in bytes */
			if (result > 32*1024) result = (result / 1024);
		}

		if (result < 32) result = defvalue;
	}
	else {
		enum msgchannels_t i;
		unsigned int isz;

		result = 0;

		for (i=C_STATUS; (i < C_LAST); i++) {
			isz = shbufsz(i);
			if (isz > result) result = isz;
		}
	}

	return result;
}

