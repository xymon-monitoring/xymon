/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DNSBITS_H__
#define __DNSBITS_H__

extern void dns_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen);
extern void dns_print_response(unsigned char *abuf, int alen, strbuffer_t *log);
extern int dns_name_type(char *name);
extern int dns_name_class(char *name);

#endif

