/*----------------------------------------------------------------------------*/
/* Hobbit mail-acknowledgment filter.                                         */
/*                                                                            */
/* This program runs from the Hobbit users' .procmailrc file, and processes   */
/* incoming e-mails that are responses to alert mails that Hobbit has sent    */
/* out. It was inspired by the functionality of the bb-mailack.sh.            */
/*                                                                            */
/* Copyright (C) 2005 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-mailack.c,v 1.1 2005-02-23 16:16:55 henrik Exp $";

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <pcre.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	char buf[4096];
	char *subjectline = NULL;
	char *returnpathline = NULL;
	char *fromline = NULL;
	char *firsttxtline = NULL;
	int inheaders = 1;
	char *p;
	pcre *subjexp;
	const char *errmsg;
	int errofs, result;
	int ovector[30];
	char cookie[10];
	int duration;
	int argi;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
	}

	while (fgets(buf, sizeof(buf), stdin)) {
		p = buf + strcspn(buf, "\r\n"); *p = '\0';

		if (!inheaders) {
			/* Save the first line of the message body, but ignore blank lines */
			if (*buf && !firsttxtline) firsttxtline = strdup(buf);
			continue;	/* We dont care about the rest of the message body */
		}

		/* See if we're at the end of the mail headers */
		if (inheaders && (strlen(buf) == 0)) { inheaders = 0; continue; }

		/* Is it one of those we want to keep ? */
		if (strncasecmp(buf, "return-path:", 12) == 0) returnpathline = strdup(skipwhitespace(buf+12));
		else if (strncasecmp(buf, "from:", 5) == 0)    fromline = strdup(skipwhitespace(buf+5));
		else if (strncasecmp(buf, "subject:", 8) == 0) subjectline = strdup(skipwhitespace(buf+8));
	}

	/* No subject ? No deal */
	if (subjectline == NULL) {
		dprintf("Subject-line not found\n");
		return 1;
	}

	/* Get the alert cookie */
	subjexp = pcre_compile(".*BB \\[([0-9]+)\\] .+:.+", PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (subjexp == NULL) {
		dprintf("PCRE failed to compile subject pattern\n");
		return 2;
	}

	result = pcre_exec(subjexp, NULL, subjectline, strlen(subjectline), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
	if (result < 0) {
		dprintf("Subject line did not match pattern\n");
		return 3; /* Subject did not match what we expected */
	}
	if (pcre_copy_substring(subjectline, ovector, result, 1, cookie, sizeof(cookie)) <= 0) {
		dprintf("Could not find cookie value\n");
		return 4; /* No cookie */
	}

	/* See if there's a delay-value also */
	p = strstr(subjectline, "DELAY="); duration = (p ? atoi(p+6) : 30);

	/* Use the "return-path:" header if we didn't see a From: line */
	if ((fromline == NULL) && returnpathline) fromline = returnpathline;

	/* Setup the acknowledge message */
	p = buf;
	p += sprintf(p, "hobbitdack %s %d %s", cookie, duration, firsttxtline);
	if (fromline) {
		p += sprintf(p, "\nAcked by: %s", fromline);
	}

	if (debug) {
		printf("%s\n", buf);
		return 0;
	}

	sendmessage(buf, NULL, NULL, NULL, 0, 30);
	return 0;
}

