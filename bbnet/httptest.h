/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the testing of a HTTP service.                   */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HTTPTESTT_H_
#define __HTTPTEST_H_

typedef struct url_t {
	char *protocol;
	char *host;
	char *auth;
	char *relurl;

	char *ip;
	int  port;
} url_t;

extern int  tcp_http_data_callback(unsigned char *buf, unsigned int len, void *priv);
extern void tcp_http_final_callback(void *priv);
extern void add_http_test(testitem_t *t, ssloptions_t *sslopt);

#endif

