/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* Copyright (C) 2002-2011 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HEADFOOT_H__
#define __HEADFOOT_H__

#include <time.h>

extern void sethostenv(char *host, char *ip, char *svc, char *color, char *hikey);
extern void sethostenv_report(time_t reportstart, time_t reportend, double repwarn, double reppanic);
extern void sethostenv_snapshot(time_t snapshot);
extern void sethostenv_histlog(char *histtime);
extern void sethostenv_template(char *dir);
extern void sethostenv_refresh(int n);
extern void sethostenv_filter(char *hostptn, char *pageptn, char *ipptn, char *classptn);
extern void sethostenv_clearlist(char *listname);
extern void sethostenv_addtolist(char *listname, char *name, char *val, char *extra, int selected);
extern void sethostenv_critack(int prio, char *ttgroup, char *ttextra, char *infourl, char *docurl);
extern void sethostenv_critedit(char *updinfo, int prio, char *group, time_t starttime, time_t endtime, char *crittime, char *extra);
extern void sethostenv_critclonelist_clear(void);
extern void sethostenv_critclonelist_add(char *hostname);
extern void sethostenv_pagepath(char *s);
extern void sethostenv_backsecs(int seconds);
extern void sethostenv_eventtime(time_t starttime, time_t endtime);
extern void output_parsed(FILE *output, char *templatedata, int bgcolor, time_t selectedtime);
extern void headfoot(FILE *output, char *template, char *pagepath, char *head_or_foot, int bgcolor);
extern void showform(FILE *output, char *headertemplate, char *formtemplate, int color, time_t seltime, char *pretext, char *posttext);

#endif

