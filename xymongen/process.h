/*----------------------------------------------------------------------------*/
/* Xymon overview webpage generator tool.                                     */
/*                                                                            */
/* Copyright (C) 2002-2010 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __PROCESS_H_
#define __PROCESS_H_

extern void calc_hostcolors(char *nongreenignores);
extern void calc_pagecolors(xymongen_page_t *phead);
extern void delete_old_acks(void);
extern void send_summaries(summary_t *sumhead);

#endif

