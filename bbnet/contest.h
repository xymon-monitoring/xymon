/*----------------------------------------------------------------------------*/
/* Big Brother network test tool.                                             */
/*                                                                            */
/* This is used to implement the testing of a TCP service.                    */
/*                                                                            */
/* Copyright (C) 2003 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __CONTEST_H_
#define __CONTEST_H_

extern test_t *add_tcp_test(char *ip, int portnum, char *service, int silent);
extern void do_tcp_tests(int timeout, int concurrency);
extern void show_tcp_test_results(void);
extern int tcp_got_expected(test_t *test);

#endif

