/*----------------------------------------------------------------------------*/
/* Xymon CGI proxy.                                                           */
/*                                                                            */
/* This CGI can gateway a Xymon message sent via HTTP PORT to a Xymon         */
/* server running on the local host.                                          */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include "libxymon.h"

int main(int argc, char *argv[])
{
	int result = 1;
	cgidata_t *cgidata = NULL;
	sendreturn_t *sres;
	char *xymsrv;

	/*
	 * Honor the XYMSRV environment variable so that a single CGI
	 * deployment (typically fronted by Apache) can forward messages
	 * to different xymond backends -- e.g. for per-vhost multi-tenant
	 * routing. Falls back to 127.0.0.1 when not set, preserving the
	 * original single-host behavior.
	 */
	xymsrv = getenv("XYMSRV");
	if (!xymsrv || !*xymsrv) xymsrv = "127.0.0.1";

	cgidata = cgi_request();
	if (cgidata) {
		printf("Content-Type: application/octet-stream\n\n");
		sres = newsendreturnbuf(1, stdout);
		result = sendmessage(cgidata->value, xymsrv, XYMON_TIMEOUT, sres);
	}

	return result;
}

