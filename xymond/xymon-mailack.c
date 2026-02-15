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
#include <stdlib.h>

#include "pcre_compat.h"

#include "libxymon.h"

int main(int argc, char *argv[])
{
        strbuffer_t *inbuf;
        char *ackbuf = NULL;
        char *subjectline = NULL;
        char *returnpathline = NULL;
        char *fromline = NULL;
        char *firsttxtline = NULL;
        int firsttxtline_alloc = 0;
        int inheaders = 1;
        char *p;
        pcre_match_data_t *match_data = NULL;
        char cookie[10];
        int duration = 0;
        int argi;
        char *envarea = NULL;
        int rc = 0;

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
                        if (envarea) free(envarea);
                        envarea = strdup(p+1);
                }
        }

        initfgets(stdin);
        inbuf = newstrbuffer(0);
        while (unlimfgets(inbuf, stdin)) {
                sanitize_input(inbuf, 0, 0);

                if (!inheaders) {
                        if ((strncasecmp(STRBUF(inbuf), "delay=", 6) == 0) || (strncasecmp(STRBUF(inbuf), "delay ", 6) == 0)) {
                                duration = durationvalue(STRBUF(inbuf)+6);
                                continue;
                        }
                        else if ((strncasecmp(STRBUF(inbuf), "ack=", 4) == 0) || (strncasecmp(STRBUF(inbuf), "ack ", 4) == 0)) {
                                if (subjectline) free(subjectline);
                                subjectline = malloc(STRBUFLEN(inbuf) + 1024);
                                sprintf(subjectline, "Subject: Xymon [%s]", STRBUF(inbuf)+4);
                        }
                        else if (*STRBUF(inbuf) && !firsttxtline) {
                                firsttxtline = strdup(STRBUF(inbuf));
                                firsttxtline_alloc = 1;
                        }

                        continue;
                }

                if (inheaders && (STRBUFLEN(inbuf) == 0)) { inheaders = 0; continue; }

                if (strncasecmp(STRBUF(inbuf), "return-path:", 12) == 0) {
                        if (returnpathline) free(returnpathline);
                        returnpathline = strdup(skipwhitespace(STRBUF(inbuf)+12));
                }
                else if (strncasecmp(STRBUF(inbuf), "from:", 5) == 0) {
                        if (fromline && (fromline != returnpathline)) free(fromline);
                        fromline = strdup(skipwhitespace(STRBUF(inbuf)+5));
                }
                else if (strncasecmp(STRBUF(inbuf), "subject:", 8) == 0) {
                        if (subjectline) free(subjectline);
                        subjectline = strdup(skipwhitespace(STRBUF(inbuf)+8));
                }
        }
        freestrbuffer(inbuf);

        if (subjectline == NULL) {
                dbgprintf("Subject-line not found\n");
                rc = 1;
                goto cleanup;
        }

	if (match_and_extract(subjectline, ".*(Xymon|Hobbit|BB)[ -]* \\[*(-*[0-9]+)[\\]!]*", 2, cookie, sizeof(cookie), NULL, &match_data) < 0) {
            dbgprintf("Subject line did not match pattern\n");
                rc = 3;
                goto cleanup;
        }

	if (match_and_extract(subjectline, ".*DELAY[ =]+([0-9]+[mhdw]*)", 1, NULL, 0, NULL, &match_data) >= 0) {
                char delaytxt[4096];
		if (match_and_extract(subjectline, ".*DELAY[ =]+([0-9]+[mhdw]*)", 1, delaytxt, sizeof(delaytxt), NULL, &match_data) >= 0) {
                        duration = durationvalue(delaytxt);
                }
        }

	if (match_and_extract(subjectline, ".*MSG[ =]+(.*)", 1, NULL, 0, NULL, &match_data) >= 0) {

                char msgtxt[4096];
		if (match_and_extract(subjectline, ".*MSG[ =]+(.*)", 1, msgtxt, sizeof(msgtxt), NULL, &match_data) >= 0) {
                        if (firsttxtline && firsttxtline_alloc) free(firsttxtline);
                        firsttxtline = strdup(msgtxt);
                        firsttxtline_alloc = 1;
                }
        }

        if ((fromline == NULL) && returnpathline) fromline = returnpathline;
        if (fromline) {
                while ((p = strchr(fromline, '<')) != NULL) *p = ' ';
                while ((p = strchr(fromline, '>')) != NULL) *p = ' ';
        }

        if (duration == 0) duration = 60;
        if (firsttxtline == NULL) {
                firsttxtline = "<No cause specified>";
                firsttxtline_alloc = 0;
        }

        ackbuf = malloc(4096 + strlen(firsttxtline) + (fromline ? strlen(fromline) : 0));
        p = ackbuf;
        p += sprintf(p, "xymondack %s %d %s", cookie, duration, firsttxtline);
        if (fromline) {
                p += sprintf(p, "\nAcked by: %s", fromline);
        }

        if (debug) {
                printf("%s\n", ackbuf);
                rc = 0;
                goto cleanup;
        }

        sendmessage(ackbuf, NULL, XYMON_TIMEOUT, NULL);
        rc = 0;

cleanup:
        if (match_data) pcre_match_data_free_compat(match_data);
        if (ackbuf) free(ackbuf);
        if (subjectline) free(subjectline);

        if (fromline && (fromline != returnpathline)) free(fromline);
        if (returnpathline) free(returnpathline);

        if (firsttxtline && firsttxtline_alloc) free(firsttxtline);
        if (envarea) free(envarea);

        return rc;
}

