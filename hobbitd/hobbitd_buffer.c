/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* This module contains a shared routine to find the size of a shared memory  */
/* buffer used for one of the Hobbit communications-channels.                 */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_buffer.c,v 1.1 2005-08-13 15:38:43 henrik Exp $";

#include <unistd.h>
#include <stdlib.h>

#include "libbbgen.h"
#include "hobbitd_buffer.h"

unsigned int shbufsz(enum msgchannels_t chnid)
{
	unsigned int defvalue, result = 0;
	char *v;

	if (chnid != C_LAST) {
		switch (chnid) {
		  case C_STATUS: v = getenv("MAXMSG_STATUS"); defvalue = 256; break;
		  case C_STACHG: v = getenv("MAXMSG_STACHG"); defvalue = 256; break;
		  case C_PAGE:   v = getenv("MAXMSG_PAGE");   defvalue = 256; break;
		  case C_DATA:   v = getenv("MAXMSG_DATA");   defvalue = 256; break;
		  case C_NOTES:  v = getenv("MAXMSG_NOTES");  defvalue = 256; break;
		  case C_ENADIS: v = getenv("MAXMSG_ENADIS"); defvalue = 256; break;
		  case C_CLIENT: v = getenv("MAXMSG_CLIENT"); defvalue = 1024; break;
		  default: break;
		}

		if (v) result = atoi(v);
		if (result < 128) result = defvalue;
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

	return 1024*result;
}

