/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tests/xymonnet/weburl-free-harness.c
 *
 * Decode a spread of web-test specs through the real decode_url() and hand
 * each result to free_weburl(). The runner checks it for leaks with whichever
 * tool is at hand (Valgrind through build/valgrind.sh, else AddressSanitizer):
 * anything decode_url() allocated that free_weburl() does not release is
 * reported and the run fails. The two passes also catch a double free.
 */

#include <stdlib.h>
#include <string.h>

#include "libxymon.h"

static void one(const char *spec)
{
	weburl_t u;
	char *s = strdup(spec);	/* decode_url() may write into its argument */

	decode_url(s, &u);
	free_weburl(&u);
	free(s);
}

int main(void)
{
	/*
	 * Each spec reaches a different set of the allocations decode_url()
	 * makes: a column name, basic-auth in the URL, post and expect data, a
	 * content type, OK/not-OK status codes, and a spec that fails to parse.
	 */
	static const char *specs[] = {
		"http://bob:pw1@127.0.0.1:18090/page.txt",
		"cont=mycol;http://127.0.0.1:18090/page.txt;alive",
		"nocont=c;https://u:p@host.example:8443/a/b?x=1;absent",
		"post=pc;http://host/post;name=val;text/plain;ok",
		"httpstatus=st;http://127.0.0.1:18090/missing;200;404",
		"content=http://127.0.0.1/plain",
		"soap=s;http://host/svc;<r/>;reply",
		"http:no-slashes",
		NULL
	};
	int pass, i;

	for (pass = 0; pass < 2; pass++)
		for (i = 0; specs[i]; i++) one(specs[i]);

	return 0;
}
