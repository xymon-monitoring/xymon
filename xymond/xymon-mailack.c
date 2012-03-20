/*----------------------------------------------------------------------------*/
/* Xymon mail-acknowledgment filter.                                          */
/*                                                                            */
/* This program runs from the Xymon users' .procmailrc file, and processes    */
/* incoming e-mails that are responses to alert mails that Xymon has sent     */
/* out.                                                                       */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <pcre.h>

#include "libxymon.h"

int main(int argc, char *argv[])
{
	strbuffer_t *inbuf;
	char *ackbuf;
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
		if (standardoption(argv[0], argv[argi])) {
			if (showhelp) return 0;
		}
	}

	initfgets(stdin);
	inbuf = newstrbuffer(0);
	while (unlimfgets(inbuf, stdin)) {
		sanitize_input(inbuf, 0, 0);

		if (!inheaders) {
			/* We're in the message body. Look for a "delay=N" line here. */
			if ((strncasecmp(STRBUF(inbuf), "delay=", 6) == 0) || (strncasecmp(STRBUF(inbuf), "delay ", 6) == 0)) {
				duration = durationvalue(STRBUF(inbuf)+6);
				continue;
			}
			else if ((strncasecmp(STRBUF(inbuf), "ack=", 4) == 0) || (strncasecmp(STRBUF(inbuf), "ack ", 4) == 0)) {
				/* Some systems cannot generate a subject. Allow them to ack
				 * via text in the message body. */
				subjectline = (char *)malloc(STRBUFLEN(inbuf) + 1024);
				sprintf(subjectline, "Subject: Xymon [%s]", STRBUF(inbuf)+4);
			}
			else if (*STRBUF(inbuf) && !firsttxtline) {
				/* Save the first line of the message body, but ignore blank lines */
				firsttxtline = strdup(STRBUF(inbuf));
			}

			continue;	/* We dont care about the rest of the message body */
		}

		/* See if we're at the end of the mail headers */
		if (inheaders && (STRBUFLEN(inbuf) == 0)) { inheaders = 0; continue; }

		/* Is it one of those we want to keep ? */
		if (strncasecmp(STRBUF(inbuf), "return-path:", 12) == 0) returnpathline = strdup(skipwhitespace(STRBUF(inbuf)+12));
		else if (strncasecmp(STRBUF(inbuf), "from:", 5) == 0)    fromline = strdup(skipwhitespace(STRBUF(inbuf)+5));
		else if (strncasecmp(STRBUF(inbuf), "subject:", 8) == 0) subjectline = strdup(skipwhitespace(STRBUF(inbuf)+8));
	}
	freestrbuffer(inbuf);

	/* No subject ? No deal */
	if (subjectline == NULL) {
		dbgprintf("Subject-line not found\n");
		return 1;
	}

	/* Get the alert cookie */
	subjexp = pcre_compile(".*(Xymon|Hobbit|BB)[ -]* \\[*(-*[0-9]+)[\\]!]*", PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (subjexp == NULL) {
		dbgprintf("pcre compile failed - 1\n");
		return 2;
	}
	result = pcre_exec(subjexp, NULL, subjectline, strlen(subjectline), 0, 0, ovector, (sizeof(ovector)/sizeof(int)));
	if (result < 0) {
		dbgprintf("Subject line did not match pattern\n");
		return 3; /* Subject did not match what we expected */
	}
	if (pcre_copy_substring(subjectline, ovector, result, 2, cookie, sizeof(cookie)) <= 0) {
		dbgprintf("Could not find cookie value\n");
		return 4; /* No cookie */
	}
	pcre_free(subjexp);

	/* See if there's a "DELAY=" delay-value also */
	subjexp = pcre_compile(".*DELAY[ =]+([0-9]+[mhdw]*)", PCRE_CASELESS, &errmsg, &errofs, NULL);
	if (subjexp == NULL) {
		dbgprintf("pcre compile failed - 2\n");
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
		dbgprintf("pcre compile failed - 3\n");
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
	if (firsttxtline == NULL) firsttxtline = "<No cause specified>";
	ackbuf = (char *)malloc(4096 + strlen(firsttxtline) + (fromline ? strlen(fromline) : 0));
	p = ackbuf;
	p += sprintf(p, "xymondack %s %d %s", cookie, duration, firsttxtline);
	if (fromline) {
		p += sprintf(p, "\nAcked by: %s", fromline);
	}

	if (debug) {
		printf("%s\n", ackbuf);
		return 0;
	}

	sendmessage(ackbuf, NULL, XYMON_TIMEOUT, NULL);
	return 0;
}

