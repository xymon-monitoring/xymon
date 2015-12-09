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

	cgidata = cgi_request();
	if (!cgidata || !cgidata->valsize) {
		printf("Status: 400\nContent-Type: text/plain\n\nNo request submitted\n");
		return 1;
	}

	/* NB: We're blasting the output regardless of success here */
	printf("Content-Type: application/octet-stream\n\n");
	sres = newsendreturnbuf(1, stdout);
	result = sendmessage_safe(cgidata->value, cgidata->valsize, "127.0.0.1", XYMON_TIMEOUT, sres);

	if (result != XYMONSEND_OK) {
		printf("Attempted msg post failed: %s\n", strxymonsendresult(result));
		errprintf("Attempted msg post failed: %s\n", strxymonsendresult(result));
	}
	return result;
}

