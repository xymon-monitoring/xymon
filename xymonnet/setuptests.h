/*----------------------------------------------------------------------------*/
/* Xymon monitor network test tool.                                           */
/*                                                                            */
/* Copyright (C) 2004-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __SETUPTESTS_H__
#define __SETUPTESTS_H__

extern void test_nonet_hosts(int testthem);
extern void add_wanted_host(char *hostname);
extern void clear_wanted_hosts(void);
extern int read_tests_from_hostscfg(int defaulttimeout);
extern int setup_tests_from_database(int pingenabled, int forcetest);

#endif

