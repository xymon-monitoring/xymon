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

#include "pcre_compat.h"

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
	pcre_pattern_t *subjexp;
	pcre_match_data_t *match_data = NULL;
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

			continue;	/* We don't care about the rest of the message body */
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
	if (match_and_extract(subjectline, ".*(Xymon|Hobbit|BB)[ -]* \\[*(-*[0-9]+)[\\]!]*", 2, cookie, sizeof(cookie), &match_data) < 0) {
		dbgprintf("Subject line did not match pattern\n");
		return 3;
	}

	/* See if there's a "DELAY=" delay-value also */
	if (match_and_extract(subjectline, ".*DELAY[ =]+([0-9]+[mhdw]*)", 1, NULL, 0, &match_data) >= 0) {
		char delaytxt[4096];
		if (match_and_extract(subjectline, ".*DELAY[ =]+([0-9]+[mhdw]*)", 1, delaytxt, sizeof(delaytxt), &match_data) >= 0) {
			duration = durationvalue(delaytxt);
		}
	}

	/* See if there's a "msg" text also */
	if (match_and_extract(subjectline, ".*MSG[ =]+(.*)", 1, NULL, 0, &match_data) >= 0) {
		char msgtxt[4096];
		if (match_and_extract(subjectline, ".*MSG[ =]+(.*)", 1, msgtxt, sizeof(msgtxt), &match_data) >= 0) {
			firsttxtline = strdup(msgtxt);
		}
	}

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
	pcre_match_data_free_compat(match_data);
	return 0;
}

