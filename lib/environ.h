/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __ENVIRON_H__
#define __ENVIRON_H__

extern char *xgetenv(const char *name);
extern void envcheck(char *envvars[]);
extern void loadenv(char *envfile, char *area);
extern char *getenv_default(char *envname, char *envdefault, char **buf);
extern char *expand_env(char *s);

#endif

