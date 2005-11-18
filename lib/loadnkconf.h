/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module for Hobbit, responsible for loading the           */
/* hobbit-nkview.cfg file.                                                    */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LOADNKCONF_H__
#define __LOADNKCONF_H__

typedef struct nkconf_t {
	char *key;
	int priority;
	char *ttgroup;
	char *ttextra;
} nkconf_t;

extern int load_nkconfig(char *fn, char *wantclass);
extern nkconf_t *get_nkconfig(char *key);

#endif

