/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CGI_H__
#define __CGI_H__

typedef struct cgidata_t {
	char *name;
	char *value;
	char *filename;
	struct cgidata_t *next;
} cgidata_t;

enum cgi_method_t { CGI_OTHER, CGI_GET, CGI_POST };
extern enum cgi_method_t cgi_method;

/*
 * The character set a Xymon canonical hostname (and the CGI host/service
 * parameters derived from it) may use: ASCII letters and digits plus the
 * punctuation real names need -- ':' (IPv6), ',' (the URL spelling of '.'),
 * '.' (FQDNs / host.service), '_' and '-'. Single point of record so the
 * hosts.cfg loader, the xymond ghost-name guard and the web CGIs all accept
 * the same set; widening it (e.g. to UTF-8) is a deliberate policy change
 * made here, once. See issue #309.
 */
#define XYMON_HOSTNAME_CHARS "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:,._-"

extern char *cgi_error(void);
extern int cgi_ispost(void);
extern cgidata_t *cgi_request(void);
extern char *csp_header(const char *pagename); 
extern int cgi_refererok(char *expected); 
extern char *get_cookie(char *cookiename);
extern char *safe_basename(char *path);
extern int cgi_hasctrl(const char *value);
extern char *cgi_pathcomponent(const char *value, int decode_commas);
extern char *cgi_component(const char *value);
extern void cgi_split_hostsvc(char *value, char **hostname, char **service);

#endif

