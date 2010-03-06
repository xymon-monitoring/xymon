/*----------------------------------------------------------------------------*/
/* Hobbit CGI proxy.                                                          */
/*                                                                            */
/* This CGI can gateway a Hobbit/BB message sent via HTTP PORT to a Hobbit    */
/* server running on the local host.                                          */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbmessage.c 6125 2009-02-12 13:09:34Z storner $";

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	int result = 1;
	cgidata_t *cgidata = NULL;
	sendreturn_t *sres;

	cgidata = cgi_request();
	if (cgidata) {
		printf("Content-Type: application/octet-stream\n\n");
		sres = newsendreturnbuf(1, stdout);
		result = sendmessage(cgidata->value, "127.0.0.1", BBTALK_TIMEOUT, sres);
	}

	return result;
}

