/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2011 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __WEBACCESS_H__
#define __WEBACCESS_H__

typedef enum { WEB_ACCESS_VIEW, WEB_ACCESS_CONTROL, WEB_ACCESS_ADMIN } web_access_type_t;

extern void *load_web_access_config(char *accessfn);
extern int web_access_allowed(char *username, char *hostname, char *testname, web_access_type_t acc);

#endif
