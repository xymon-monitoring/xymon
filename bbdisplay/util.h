/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __UTIL_H_
#define __UTIL_H_

#include <stdio.h>

typedef struct linebuf_t {
	char *buf;
	int buflen;
} linebuf_t;

typedef struct urlelem_t {
	char *origform;
	char *scheme;
	char *schemeopts;
	char *host;
	char *ip;
	int  port;
	char *auth;
	char *relurl;
	int parseerror;
} urlelem_t;

enum bbtesttype_t { 
	BBTEST_PLAIN, BBTEST_CONTENT, BBTEST_CONT, BBTEST_NOCONT, BBTEST_POST, BBTEST_NOPOST, BBTEST_TYPE 
};

typedef struct bburl_t {
	int testtype;
	char *columnname;
	struct urlelem_t *desturl;
	struct urlelem_t *proxyurl;
	unsigned char *postdata;
	unsigned char *expdata;
} bburl_t;

extern char *read_line(struct linebuf_t *buffer, FILE *stream);

extern int use_recentgifs;
extern int unpatched_bbd;
extern char timestamp[];

extern void errprintf(const char *fmt, ...);
extern char *errbuf;
extern int save_errbuf;

extern FILE *stackfopen(char *filename, char *mode);
extern int stackfclose(FILE *fd);
extern char *stackfgets(char *buffer, unsigned int bufferlen, char *includetag1, char *includetag2);

extern char *malcop(const char *s);
extern int get_fqdn(void);
extern void init_timestamp(void);
extern int argnmatch(char *arg, char *match);
extern char *skipword(char *l);
extern char *skipwhitespace(char *l);

extern char *colorname(int color);
extern int parse_color(char *colortext);
extern int eventcolor(char *colortext);
extern char *dotgiffilename(int color, int acked, int oldage);
extern char *weekday_text(char *dayspec);
extern char *time_text(char *timespec);
extern char *alttag(entry_t *e);
extern char *hostpage_link(host_t *host);
extern char *hostpage_name(host_t *host);
extern char *commafy(char *hostname);
extern void sethostenv(char *host, char *ip, char *svc, char *color);
extern void sethostenv_report(time_t reportstart, time_t reportend, double repwarn, double reppanic);
extern void sethostenv_snapshot(time_t snapshot);
extern void headfoot(FILE *output, char *pagetype, char *pagepath, char *head_or_foot, int bgcolor);
extern void do_bbext(FILE *output, char *extenv, char *family);
extern int checkalert(char *alertlist, char *test);
extern int checkpropagation(host_t *host, char *test, int color, int acked);

extern link_t *find_link(const char *name);
extern char *columnlink(link_t *link, char *colname);
extern char *hostlink(link_t *link);
extern char *urldoclink(const char *docurl, const char *hostname);
extern char *cleanurl(char *url);

extern host_t *find_host(const char *hostname);
extern bbgen_col_t *find_or_create_column(const char *testname, int create);
extern char *histlogurl(char *hostname, char *service, time_t histtime);

extern int within_sla(char *hostline, char *tag, int defresult);
extern int periodcoversnow(char *tag);

extern void envcheck(char *envvars[]);

extern int run_columngen(char *column, int update_interval, int enabled);
extern void drop_genstatfiles(void);

extern int generate_static(void);
extern int stdout_on_file(char *filename);
extern void setup_signalhandler(char *programname);

extern int hexvalue(unsigned char c);
extern char *urlencode(char *s);
extern char *urldecode(char *envvar);
extern char *urlunescape(char *url);
extern int urlvalidate(char *query, char *validchars);
extern time_t sslcert_expiretime(char *timestr);

extern unsigned int IPtou32(int ip1, int ip2, int ip3, int ip4);
extern char *u32toIP(unsigned int ip32);

extern void addtobuffer(char **buf, int *buflen, char *newtext);
extern int run_command(char *cmd, char *errortext, char **banner, int *bannerbytes, int showcmd);

extern struct timeval *tvdiff(struct timeval *tstart, struct timeval *tend, struct timeval *result);

extern char *base64encode(unsigned char *buf);
extern char *base64decode(unsigned char *buf);
extern void getescapestring(char *msg, unsigned char **buf, int *buflen);
extern char *decode_url(char *inputurl, bburl_t *bburl);
extern char *msg_data(char *msg);
extern void getenv_default(char *envname, char *envdefault, char **buf);
extern char *gettok(char *s, char *delims);

#endif

