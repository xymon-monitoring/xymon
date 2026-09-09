/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * tests/server/sendmsg-empty-recipient-harness.c
 *
 * One call into sendmessage(), with the recipient left to $XYMSRV, printing the
 * sendresult_t it came back with. The caller supplies the environment and
 * judges the answer: sendmessage() caches $XYMSRV in a static on its first
 * call, so each case has to be its own process.
 */

#include <stdio.h>
#include <stdlib.h>
#include "libxymon.h"

static const char *resultname(sendresult_t r)
{
	switch (r) {
	  case XYMONSEND_OK:               return "XYMONSEND_OK";
	  case XYMONSEND_EBADIP:           return "XYMONSEND_EBADIP";
	  case XYMONSEND_EIPUNKNOWN:       return "XYMONSEND_EIPUNKNOWN";
	  case XYMONSEND_ENOSOCKET:        return "XYMONSEND_ENOSOCKET";
	  case XYMONSEND_ECANNOTDONONBLOCK: return "XYMONSEND_ECANNOTDONONBLOCK";
	  case XYMONSEND_ECONNFAILED:      return "XYMONSEND_ECONNFAILED";
	  case XYMONSEND_ESELFAILED:       return "XYMONSEND_ESELFAILED";
	  case XYMONSEND_ETIMEOUT:         return "XYMONSEND_ETIMEOUT";
	  case XYMONSEND_EWRITEERROR:      return "XYMONSEND_EWRITEERROR";
	  case XYMONSEND_EREADERROR:       return "XYMONSEND_EREADERROR";
	  case XYMONSEND_EBADURL:          return "XYMONSEND_EBADURL";
	}

	return "XYMONSEND_UNRECOGNISED";
}

int main(int argc, char **argv)
{
	sendreturn_t *response;
	sendresult_t res;
	int timeout = (argc > 1) ? atoi(argv[1]) : 2;

	response = newsendreturnbuf(1, NULL);
	res = sendmessage("ping", NULL, timeout, response);
	printf("result=%s\n", resultname(res));
	freesendreturnbuf(response);

	return 0;
}
