/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2005 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MISC_H__
#define __MISC_H__

#include <stdio.h>

enum ostype_t { OS_UNKNOWN, OS_SOLARIS, OS_OSF, OS_AIX, OS_HPUX, OS_WIN32, OS_FREEBSD, OS_NETBSD, OS_OPENBSD, OS_LINUX22, OS_LINUX, OS_RHEL3, OS_SNMP, OS_IRIX } ;

extern enum ostype_t get_ostype(char *osname);
extern int hexvalue(unsigned char c);
extern char *commafy(char *hostname);
extern char *skipword(char *l);
extern char *skipwhitespace(char *l);
extern int argnmatch(char *arg, char *match);
extern void addtobuffer(char **buf, int *bufsz, char *newtext);
extern char *msg_data(char *msg);
extern char *gettok(char *s, char *delims);
extern void grok_input(char *s);
extern unsigned int IPtou32(int ip1, int ip2, int ip3, int ip4);
extern char *u32toIP(unsigned int ip32);
extern const char *textornull(const char *text);
extern int get_fqdn(void);
extern int generate_static(void);
extern int run_command(char *cmd, char *errortext, char **banner, int *bannerbytes, int showcmd);
extern void do_bbext(FILE *output, char *extenv, char *family);
extern char **setup_commandargs(char *cmdline, char **cmd);

#if defined(HPUX) || defined(BSD) || defined(AIX)
#define atoll my_atoll
#endif

extern unsigned long long my_atoll(char *s);

#endif

