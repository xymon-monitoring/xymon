/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MISC_H__
#define __MISC_H__

#include <stdio.h>

enum ostype_t { OS_UNKNOWN, OS_SOLARIS, OS_OSF, OS_AIX, OS_HPUX, OS_WIN32, OS_FREEBSD, OS_NETBSD, OS_OPENBSD, OS_LINUX22, OS_LINUX, OS_RHEL3, OS_SNMP, OS_IRIX, OS_DARWIN, OS_SCO_SV, OS_NETWARE_SNMP, OS_WIN32_HMDC, OS_WIN32_BBWIN, OS_ZVM, OS_ZVSE } ;

extern enum ostype_t get_ostype(char *osname);
extern char *osname(enum ostype_t os);
extern int hexvalue(unsigned char c);
extern char *commafy(char *hostname);
extern void uncommafy(char *hostname);
extern char *skipword(char *l);
extern char *skipwhitespace(char *l);
extern int argnmatch(char *arg, char *match);
extern char *msg_data(char *msg);
extern char *gettok(char *s, char *delims);
extern char *wstok(char *s);
extern void sanitize_input(strbuffer_t *l, int stripcomment, int unescape);
extern unsigned int IPtou32(int ip1, int ip2, int ip3, int ip4);
extern char *u32toIP(unsigned int ip32);
extern const char *textornull(const char *text);
extern int get_fqdn(void);
extern int generate_static(void);
extern int run_command(char *cmd, char *errortext, strbuffer_t *banner, int showcmd, int timeout);
extern void do_bbext(FILE *output, char *extenv, char *family);
extern char **setup_commandargs(char *cmdline, char **cmd);
extern int checkalert(char *alertlist, char *test);

extern long long str2ll(char *s, char **errptr);

extern char *nextcolumn(char *s);
extern int selectcolumn(char *heading, char *wanted);
extern char *getcolumn(char *s, int wanted);

#endif

