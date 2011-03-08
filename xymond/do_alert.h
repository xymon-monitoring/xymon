/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2004-2010 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __DO_ALERT_H__
#define __DO_ALERT_H__

#include <time.h>
#include <stdio.h>

extern int include_configid;
extern int testonly;

extern time_t next_alert(activealerts_t *alert);
extern void cleanup_alert(activealerts_t *alert);
extern void clear_interval(activealerts_t *alert);

extern void start_alerts(void);
extern void send_alert(activealerts_t *alert, FILE *logfd);
extern void finish_alerts(void);

extern void load_state(char *filename, char *statusbuf);
extern void save_state(char *filename);

#endif

