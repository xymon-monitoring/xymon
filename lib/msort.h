/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2007 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __MSORT_H__
#define __MSORT_H__

typedef int (msortcompare_fn_t)(void *, void *);
extern void *mergesort(void *head_in, msortcompare_fn_t comparefn_in);

#endif

