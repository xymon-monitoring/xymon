/*----------------------------------------------------------------------------*/
/* Xymon message daemon.                                                      */
/*                                                                            */
/* Copyright (C) 2005-2012 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "libxymon.h"
#include "sections.h"

sectlist_t *defsecthead = NULL;

void nextsection_r_done(void *secthead)
{
	/* Free the old list */
	sectlist_t *swalk, *stmp;

	swalk = (sectlist_t *)secthead;
	while (swalk) {
		if (swalk->nextsectionrestoreptr) *swalk->nextsectionrestoreptr = swalk->nextsectionrestoreval;
		if (swalk->sectdatarestoreptr) *swalk->sectdatarestoreptr = swalk->sectdatarestoreval;

		stmp = swalk;
		swalk = swalk->next;
		xfree(stmp);
	}
}

void splitmsg_r(char *clientdata, sectlist_t **secthead)
{
	char *cursection, *nextsection;
	char *sectname, *sectdata;

	if (clientdata == NULL) {
		errprintf("Got a NULL client data message\n");
		return;
	}

	if (secthead == NULL) {
		errprintf("BUG: splitmsg_r called with NULL secthead\n");
		return;
	}

	if (*secthead) {
		errprintf("BUG: splitmsg_r called with non-empty secthead\n");
		nextsection_r_done(*secthead);
		*secthead = NULL;
	}

	/* Find the start of the first section */
	if (*clientdata == '[') 
		cursection = clientdata; 
	else {
		cursection = strstr(clientdata, "\n[");
		if (cursection) cursection++;
	}

	while (cursection) {
		sectlist_t *newsect = (sectlist_t *)calloc(1, sizeof(sectlist_t));

		/* Find end of this section (i.e. start of the next section, if any) */
		nextsection = strstr(cursection, "\n[");
		if (nextsection) {
			newsect->nextsectionrestoreptr = nextsection;
			newsect->nextsectionrestoreval = *nextsection;
			*nextsection = '\0';
			nextsection++;
		}

		/* Pick out the section name and data */
		sectname = cursection+1;
		sectdata = sectname + strcspn(sectname, "]\n");
		newsect->sectdatarestoreptr = sectdata;
		newsect->sectdatarestoreval = *sectdata;
		*sectdata = '\0'; 
		sectdata++; if (*sectdata == '\n') sectdata++;

		/* Save the pointers in the list */
		newsect->sname = sectname;
		newsect->sdata = sectdata;
		newsect->next = *secthead;
		*secthead = newsect;

		/* Next section, please */
		cursection = nextsection;
	}
}

void splitmsg_done(void)
{
	/*
	 * NOTE: This MUST be called when we're doing using a message,
	 * and BEFORE the next message is read. If called after the
	 * next message is read, the restore-pointers in the "defsecthead"
	 * list will point to data inside the NEW message, and 
	 * if the buffer-usage happens to be setup correctly, then
	 * this will write semi-random data over the new message.
	 */
	if (defsecthead) {
		/* Clean up after the previous message */
		nextsection_r_done(defsecthead);
		defsecthead = NULL;
	}
}

void splitmsg(char *clientdata)
{
	if (defsecthead) {
		errprintf("BUG: splitmsg_done() was not called on previous message - data corruption possible.\n");
		splitmsg_done();
	}

	splitmsg_r(clientdata, &defsecthead);
}

char *nextsection_r(char *clientdata, char **name, void **current, void **secthead)
{
	if (clientdata) {
		*secthead = NULL;
		splitmsg_r(clientdata, (sectlist_t **)secthead);
		*current = *secthead;
	}
	else {
		*current = (*current ? ((sectlist_t *)*current)->next : NULL);
	}

	if (*current) {
		*name = ((sectlist_t *)*current)->sname;
		return ((sectlist_t *)*current)->sdata;
	}

	return NULL;
}

char *nextsection(char *clientdata, char **name)
{
	static void *current = NULL;

	if (clientdata && defsecthead) {
		nextsection_r_done(defsecthead);
		defsecthead = NULL;
	}

	return nextsection_r(clientdata, name, &current, (void **)&defsecthead);
}


char *getdata(char *sectionname)
{
	sectlist_t *swalk;

	for (swalk = defsecthead; (swalk && strcmp(swalk->sname, sectionname)); swalk = swalk->next) ;
	if (swalk) return swalk->sdata;

	return NULL;
}


