/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* This module contains a shared routine to find the size of a shared memory  */
/* buffer used for one of the Xymon communications-channels.                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>
#include <stdlib.h>

#include "libxymon.h"
#include "xymond_buffer.h"
static int envloaded = 0;
static size_t chnbufsizes[C_LAST + 1];


size_t shbufsz(enum msgchannels_t chnid)
{
	if ((chnid < C_STATUS) || (chnid > C_LAST)) {
		errprintf("Invalid channel ID buffer requested: %d\n", chnid);
		return 0;
	}

	if (envloaded) return chnbufsizes[chnid];
	else {
		/* Load channel buffer sizes as configured from the environment */
		enum msgchannels_t i;
		size_t result, largestsz = 0;
		char *v = NULL;

		for (i=C_STATUS; (i < C_LAST); i++) {
		   switch (i) {
			case C_STATUS: v = xgetenv("MAXMSG_STATUS");	break;	/* default: 256 */
			case C_CLIENT: v = xgetenv("MAXMSG_CLIENT");	break;	/* default: 512 */
			case C_DATA:   v = xgetenv("MAXMSG_DATA");	break;	/* default: 256 */
			case C_NOTES:  v = xgetenv("MAXMSG_NOTES");	break;	/* default: 256 */
			case C_ENADIS: v = xgetenv("MAXMSG_ENADIS");	break;	/* default: 32 */
			case C_USER:   v = xgetenv("MAXMSG_USER");	break;	/* default: 128 */
			case C_PAGE:   v = (getenv("MAXMSG_PAGE")) ? xgetenv("MAXMSG_PAGE") : xgetenv("MAXMSG_STATUS"); break;
			case C_STACHG: v = (getenv("MAXMSG_STACHG")) ? xgetenv("MAXMSG_STACHG") : xgetenv("MAXMSG_STATUS"); break;
			case C_CLICHG: v = (getenv("MAXMSG_CLICHG")) ? xgetenv("MAXMSG_CLICHG") : xgetenv("MAXMSG_CLIENT"); break;
			case C_FEEDBACK_QUEUE: v = (getenv("MAXMSG_BFQ")) ? xgetenv("MAXMSG_BFQ") : xgetenv("MAXMSG_STATUS"); break;
			default: break;
		   }

		   if (v) result = atol(v);
		   else { errprintf("Invalid or missing buffer size for channel '%s'; using %d\n", channelnames[chnid], 1024); result = 1024; }

		   /* Keep track of the largest message expected to be possible */
		   if (result > largestsz) largestsz = result;

		   /* Backfeed queue needs to be slightly less (-1K)to account for msgp padding(?) */
		   // if (i == C_FEEDBACK_QUEUE) result--;

		   /* Store it */
		   chnbufsizes[i] = result;
		}

		/* This is what to ask for if you're not sure which channel you're receiving from */
		chnbufsizes[C_LAST] = largestsz;
		envloaded = 1;
	}

	return chnbufsizes[chnid];
}

