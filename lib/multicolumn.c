/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module, part of libxymon.                                */
/* It contains routines for status-messages to multiple columns               */
/*                                                                            */
/* Copyright (C) 2002-2012 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: sendmsg.c 7016 2012-07-09 10:50:16Z storner $";

#include <string.h>
#include <stdlib.h>

#include "libxymon.h"

typedef struct mcm_data_t {
	char *columnname;
	int color;
	strbuffer_t *mcm_summary[COL_COUNT];
	strbuffer_t *mcm_text;
	struct mcm_data_t *next;
} mcm_data_t;
mcm_data_t *mcmhead = NULL;

void clear_multicolumn_message(void)
{
	mcm_data_t *walk;

	walk = mcmhead;
	while (walk) {
		mcm_data_t *zombie;
		int i;

		zombie = walk; walk = walk->next;

		for (i = 0; (i < COL_COUNT); i++) {
			freestrbuffer(zombie->mcm_summary[i]);
		}
		freestrbuffer(zombie->mcm_text);
		xfree(zombie->columnname);
	}

	mcmhead = NULL;
}

void add_multicolumn_message(char *columnname, int color, char *txt, char *summarytxt)
{
	mcm_data_t *mdata;

	for (mdata = mcmhead; (mdata && (strcmp(mdata->columnname, columnname) != 0)); mdata = mdata->next) ;
	if (!mdata) {
		mdata = (mcm_data_t *)calloc(1, sizeof(mcm_data_t));
		mdata->columnname = strdup(columnname);
		mdata->color = color;
		mdata->next = mcmhead;
		mcmhead = mdata;
	}

	if (summarytxt) {
		if (!mdata->mcm_summary[color]) mdata->mcm_summary[color] = newstrbuffer(0);
		addtobuffer(mdata->mcm_summary[color], txt);
	}

	if (txt) {
		if (!mdata->mcm_text) mdata->mcm_text = newstrbuffer(0);
		addtobuffer(mdata->mcm_text, txt);
	}

	if (color > mdata->color) mdata->color = color;
}

void flush_multicolumn_message(char *hostname, char *line1txt, char *fromline, char *alertgroups)
{
	mcm_data_t *walk;
	char msgline[4096];

	for (walk = mcmhead; (walk); walk = walk->next) {
		char *group;

		init_status(walk->color);

		if (alertgroups) snprintf(msgline, sizeof(msgline), "status/group:%s ", alertgroups); else strcpy(msgline, "status ");
		addtostatus(msgline);

		sprintf(msgline, "%s.%s %s", commafy(hostname), walk->columnname, colorname(walk->color));
		addtostatus(msgline);

		if (line1txt) {
			addtostatus(" ");
			addtostatus(line1txt);
			addtostatus("\n");
		}

		if (walk->mcm_summary[COL_RED]) {
			addtostrstatus(walk->mcm_summary[COL_RED]);
			addtostatus("\n");
		}
		if (walk->mcm_summary[COL_YELLOW]) {
			addtostrstatus(walk->mcm_summary[COL_YELLOW]);
			addtostatus("\n");
		}
		if (walk->mcm_summary[COL_GREEN]) {
			addtostrstatus(walk->mcm_summary[COL_GREEN]);
			addtostatus("\n");
		}
		
		if (walk->mcm_text) addtostrstatus(walk->mcm_text);

		if (fromline) addtostatus(fromline);

		finish_status();
	}

	clear_multicolumn_message();
}

