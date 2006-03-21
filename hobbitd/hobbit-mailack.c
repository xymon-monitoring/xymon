/*----------------------------------------------------------------------------*/
/* Hobbit mail-acknowledgment filter.                                         */
/*                                                                            */
/* This program runs from the Hobbit users' .procmailrc file, and processes   */
/* incoming e-mails that are responses to alert mails that Hobbit has sent    */
/* out. It was inspired by the functionality of the bb-mailack.sh.            */
/*                                                                            */
/* Copyright (C) 2004-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbit-mailack.c,v 1.11 2006-03-21 21:53:35 henrik Exp $";

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <pcre.h>

#include "libbbgen.h"

int main(int argc, char *argv[])
{
	char *inbuf = NULL, *ackbuf;
	int inbufsz;
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
	int duration = 0;
	int argi;
	char *envarea = NULL;

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
	}

	initfgets(stdin);
	while (unlimfgets(&inbuf, &inbufsz, stdin)) {
		p = inbuf + strcspn(inbuf, "\r\n"); *p = '\0';

		if (!inheaders) {
			/* We're in the message body. Look for a "delay=N" line here. */
			if ((strncasecmp(inbuf, "delay=", 6) == 0) || (strncasecmp(inbuf, "delay ", 6) == 0)) {
				duration = durationvalue(inbuf+6);
				continue;
			}
			else if ((strncasecmp(inbuf, "ack=", 4) == 0) || (strncasecmp(inbuf, "ack ", 4) == 0)) {
				/* Some systems cannot generate a subject. Allow them to ack
				 * via text in the message body. */
				subjectline = (char *)malloc(1024);
				snprintf(subjectline, 1023, "Subject: Hobbit [%s]", inbuf+4);
			}
			else if (*inbuf && !firsttxtline) {
				/* Save the first line of the message body, but ignore blank lines */
				firsttxtline = strdup(inbuf);
			}

			continue;	/* We dont care about the rest of the message body */
		}

		/* See if we're at the end of the mail headers */
		if (inheaders && (strlen(inbuf) == 0)) { inheaders = 0; continue; }

		/* Is it one of those we want to keep ? */
		if (strncasecmp(inbuf, "return-path:", 12) == 0) returnpathline = strdup(skipwhitespace(inbuf+12));
		else if (strncasecmp(inbuf, "from:", 5) == 0)    fromline = strdup(skipwhitespace(inbuf+5));
		else if (strncasecmp(inbuf, "subject:", 8) == 0) subjectline = strdup(skipwhitespace(inbuf+8));
	}
	if (inbuf) xfree(inbuf);

	/* No subject ? No deal */
	if (subjectline == NULL) {
		dprintf("Subject-line not found\n");
		return 1;
	}

	/* Get the alert cookie */
	subjexp = pcre_compile(".*(Hobbit|BB)[ -]* \\[*(-*[0-9]+)[\\]!]*", PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (subjexp == NULL) {
		dprintf("pcre compile failed - 1\n");
		return 2;
	}
	result = pcre_exec(subjexp, NULL, subjectline, strlen(subjectline), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
	if (result < 0) {
		dprintf("Subject line did not match pattern\n");
		return 3; /* Subject did not match what we expected */
	}
	if (pcre_copy_substring(subjectline, ovector, result, 2, cookie, sizeof(cookie)) <= 0) {
		dprintf("Could not find cookie value\n");
		return 4; /* No cookie */
	}
	pcre_free(subjexp);

	/* See if there's a "DELAY=" delay-value also */
	subjexp = pcre_compile(".*DELAY[ =]+([0-9]+[mhdw]*)", PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (subjexp == NULL) {
		dprintf("pcre compile failed - 2\n");
		return 2;
	}
	result = pcre_exec(subjexp, NULL, subjectline, strlen(subjectline), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
	if (result >= 0) {
		char delaytxt[4096];
		if (pcre_copy_substring(subjectline, ovector, result, 1, delaytxt, sizeof(delaytxt)) > 0) {
			duration = durationvalue(delaytxt);
		}
	}
	pcre_free(subjexp);

	/* See if there's a "msg" text also */
	subjexp = pcre_compile(".*MSG[ =]+(.*)", PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (subjexp == NULL) {
		dprintf("pcre compile failed - 3\n");
		return 2;
	}
	result = pcre_exec(subjexp, NULL, subjectline, strlen(subjectline), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
	if (result >= 0) {
		char msgtxt[4096];
		if (pcre_copy_substring(subjectline, ovector, result, 1, msgtxt, sizeof(msgtxt)) > 0) {
			firsttxtline = strdup(msgtxt);
		}
	}
	pcre_free(subjexp);

	/* Use the "return-path:" header if we didn't see a From: line */
	if ((fromline == NULL) && returnpathline) fromline = returnpathline;
	if (fromline) {
		/* Remove '<' and '>' from the fromline - they mess up HTML */
		while ((p = strchr(fromline, '<')) != NULL) *p = ' ';
		while ((p = strchr(fromline, '>')) != NULL) *p = ' ';
	}

	/* Setup the acknowledge message */
	if (duration == 0) duration = 60;	/* Default: Ack for 60 minutes */
	ackbuf = (char *)malloc(4096 + strlen(firsttxtline) + (fromline ? strlen(fromline) : 0));
	p = ackbuf;
	p += sprintf(p, "hobbitdack %s %d %s", cookie, duration, firsttxtline);
	if (fromline) {
		p += sprintf(p, "\nAcked by: %s", fromline);
	}

	if (debug) {
		printf("%s\n", ackbuf);
		return 0;
	}

	sendmessage(ackbuf, NULL, NULL, NULL, 0, 30);
	return 0;
}

