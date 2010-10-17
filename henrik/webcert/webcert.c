/*----------------------------------------------------------------------------*/
/* Hobbit webpage generator tool.                                             */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: webcert.c,v 1.5 2007/08/22 13:23:27 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

#include "libbbgen.h"

char *email = NULL;
char *phone = NULL;
char *costcode = NULL;
char *validity = NULL;
char *servertype = NULL;
char *csrdata = NULL;
int  internalcert = 0;

static void errormsg(char *msg)
{
	printf("Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	printf("<html><head><title>Invalid request</title></head>\n");
	printf("<body>%s</body></html>\n", msg);
	exit(1);
}

void parse_query(void)
{
	cgidata_t *cgidata, *cwalk;

	cgidata = cgi_request();
	cwalk = cgidata;
	while (cwalk) {
		/*
		 * cwalk->name points to the name of the setting.
		 * cwakl->value points to the value (may be an empty string).
		 */

		if (strcmp(cwalk->name, "email") == 0) email = strdup(cwalk->value);
		else if (strcmp(cwalk->name, "phone") == 0) phone = strdup(cwalk->value);
		else if (strcmp(cwalk->name, "costcode") == 0) costcode = strdup(cwalk->value);
		else if (strcmp(cwalk->name, "servertype") == 0) servertype = strdup(cwalk->value);
		else if (strcmp(cwalk->name, "validity") == 0) validity = strdup(cwalk->value);
		else if (strcmp(cwalk->name, "csrtext") == 0) csrdata = strdup(cwalk->value);
		else if (strcmp(cwalk->name, "internalcert") == 0) internalcert = 1;

		cwalk = cwalk->next;
	}
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
	char *hffile = "webcert";
	int bgcolor = COL_BLUE;
	strbuffer_t *errortxt = newstrbuffer(0);
	FILE *tmpfd, *pfd;
	char csrfn[PATH_MAX];
	char cmd[PATH_MAX+100];
	char buf[10240];
	char *subj = NULL;
	char *cn = NULL;
	char *domainname = NULL;
	strbuffer_t *mbuf = newstrbuffer(0);
	strbuffer_t *whoisbuf = newstrbuffer(0);
	char mailaddr[1024];
	char *replytoenv;

	mailaddr[0] = '\0';

	for (argi = 1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--env=")) {
			char *p = strchr(argv[argi], '=');
			loadenv(p+1, envarea);
		}
		else if (argnmatch(argv[argi], "--area=")) {
			char *p = strchr(argv[argi], '=');
			envarea = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--debug") == 0) {
			debug = 1;
		}
		else {
			strcat(mailaddr, argv[argi]);
			strcat(mailaddr, " ");
		}
	}

	parse_query();

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));

	if (cgi_method == CGI_GET) {
		showform(stdout, hffile, "webcert_form", bgcolor, 0, NULL, NULL);
		return 0;
	}

	if (internalcert) {
		email = "none@sslcert.csc.com";
		phone = "0";
		costcode = "0";
		servertype = "internal";
	}

	if (!email) 
		addtobuffer(errortxt, "E-mail address missing!<br>");
	else if (!internalcert) {
		char *emaildom = strchr(email, '@');

		if (!emaildom || (strlen(emaildom+1) < 2) || (!strchr(emaildom, '.')) )
			addtobuffer(errortxt, "Invalid e-mail address!<br>");
		else {
			emaildom++;
			sprintf(cmd, "host -tMX '%s'", emaildom);
			pfd = popen(cmd, "r");
			while (fgets(buf, sizeof(buf), pfd)) ;
			if (pclose(pfd) != 0) {
				addtobuffer(errortxt, "Invalid e-mail domain!<br>");
			}
		}
	}

	if (!phone) addtobuffer(errortxt, "Phonenumber missing!<br>");
	if (!costcode) addtobuffer(errortxt, "Costcode missing!<br>");
	if (!servertype) addtobuffer(errortxt, "Servertype missing!<br>");
	if (!validity || (atoi(validity) <= 0)) addtobuffer(errortxt, "Validity invalid!<br>");
	if (!csrdata) {
		addtobuffer(errortxt, "CSR data missing!<br>");
	}
	else {
		/* Parse the CSR */
		sprintf(csrfn, "/tmp/csr.%d", getpid());
		tmpfd = fopen(csrfn, "w");
		fprintf(tmpfd, "%s", csrdata);
		fclose(tmpfd);
		sprintf(cmd, "openssl req -noout -text <%s", csrfn);
		pfd = popen(cmd, "r");
		while (fgets(buf, sizeof(buf), pfd)) {
			char *eol = strchr(buf, '\n'); if (eol) *eol = '\0';
			char *tok = strstr(buf, "Subject:");
			char *p;
	
			if (tok) {
				char *subjcopy;

				tok += strlen("Subject:");
				tok += strspn(tok, " \t");
				subj = strdup(tok);
				subjcopy = strdup(subj);

				tok = strtok(subjcopy, ",");
				while (tok) {
					tok += strspn(tok, " ");
					if (strncmp(tok, "CN=", 3) == 0) {
						p = strchr(tok, '/'); if (p) *p = '\0';
						cn = strdup(tok);
					}

					tok = strtok(NULL, ",");
				}
				xfree(subjcopy);
			}
		}
		if (pclose(pfd) != 0) {
			addtobuffer(errortxt, "Could not parse CSR data!<br>");
			subj = NULL;
		}
		else if (!subj) {
			addtobuffer(errortxt, "Invalid CSR, missing SUBJECT!<br>");
		}
		else if (!cn) {
			addtobuffer(errortxt, "Invalid CSR, missing Common Name (CN)!<br>");
		}

		domainname = cn;
		if (cn && !internalcert) {
			int res, domok = 0;

			do {
				sprintf(cmd, "host -tNS '%s'", domainname);
				pfd = popen(cmd, "r");
				while (fgets(buf, sizeof(buf), pfd)) {
					domok = (domok || strstr(buf, " name server "));
				}
				res = pclose(pfd);
				domok = (domok && (res == 0));

				if (!domok) {
					char *p = strchr(domainname, '.');
					if (p) domainname = (p+1);
				}
			} while (!domok && strchr(domainname, '.'));

			if (!domok) {
				addtobuffer(errortxt, "Domain does not exist!<br>");
			}
			else {
				addtobuffer(whoisbuf, "Whois data:\n");
				sprintf(cmd, "whois '%s'", domainname);
				pfd = popen(cmd, "r");
				while (fgets(buf, sizeof(buf), pfd)) {
					char *eol = strchr(buf, '\n'); if (eol) *eol = '\0';
					if (*buf != '#') {
						addtobuffer(whoisbuf, buf);
						addtobuffer(whoisbuf, "\n");
					}
				}
				if (pclose(pfd) != 0) {
					addtobuffer(whoisbuf, "Could not find WHOIS data\n");
				}
			}
		}

		if (!internalcert) unlink(csrfn);
	}

	if (STRBUFLEN(errortxt) > 0) {
		strbuffer_t *msg = newstrbuffer(0);

		addtobuffer(msg, "<center><font size=+1 color=\"#FF0000\">");
		addtostrbuffer(msg, errortxt);
		addtobuffer(msg, "</font></center>");
		showform(stdout, hffile, "webcert_form", bgcolor, 0, STRBUF(msg), NULL);
		return 0;
	}


	if (internalcert) {
		char certfn[PATH_MAX];

		sprintf(certfn, "/tmp/cert.%d", getpid());
		sprintf(cmd, "(echo y; echo y) | openssl ca -policy policy_anything -out %s -config /var/ca/openssl.cnf -days %d -infiles %s", 
			certfn, atoi(validity)*365, csrfn );
		pfd = popen(cmd, "r");
		while (pfd && fgets(buf, sizeof(buf), pfd)) ;
		if (pfd && (pclose(pfd) == 0)) {
			FILE *fd = fopen(certfn, "r");

			if (fd) {
				addtobuffer(mbuf, "Server Certificate:\n");
				addtobuffer(mbuf, "*******************\n");
				while (fgets(buf, sizeof(buf), fd)) addtobuffer(mbuf, buf);
				addtobuffer(mbuf, "\n\n\n");
				fclose(fd);

				addtobuffer(mbuf, "Issuer (CA) Certificate:\n");
				addtobuffer(mbuf, "************************\n");
				fd = fopen("/var/ca/private/CAcert.pem", "r");
				if (fd) {
					while (fgets(buf, sizeof(buf), fd)) addtobuffer(mbuf, buf);
					addtobuffer(mbuf, "\n");
					fclose(fd);
				}
			}
			else {
				addtobuffer(mbuf, "Could not read certificate from ");
				addtobuffer(mbuf, certfn);
				addtobuffer(mbuf, ": ");
				addtobuffer(mbuf, strerror(errno));
				addtobuffer(mbuf, "\n");
			}
		}
		else {
			addtobuffer(mbuf, "Could not generate certificate! Command: ");
			addtobuffer(mbuf, cmd);
			addtobuffer(mbuf, "\n");
		}

		unlink(csrfn);
		unlink(certfn);
	}
	else {
		sprintf(buf, "Requested by: %s (phone: %s)\n", email, phone); addtobuffer(mbuf, buf);
		sprintf(buf, "Costcode: %s\n", costcode); addtobuffer(mbuf, buf);
		sprintf(buf, "Validity: %d\n", atoi(validity)); addtobuffer(mbuf, buf);
		sprintf(buf, "Servertype: %s\n", servertype); addtobuffer(mbuf, buf);
		sprintf(buf, "Raw CSR:\n%s\n", csrdata); addtobuffer(mbuf, buf);
		addtobuffer(mbuf, "Parsed CSR info:\n");
		addtobuffer(mbuf, subj);
		addtobuffer(mbuf, "\n");
		sprintf(buf, "Common name: %s\n", cn); addtobuffer(mbuf, buf);
		sprintf(buf, "WHOIS info for domain %s\n", domainname); addtobuffer(mbuf, buf);
		addtostrbuffer(mbuf, whoisbuf);
		addtobuffer(mbuf, "\n");
	}


	headfoot(stdout, "webcert", "", "header", COL_BLUE);

	fprintf(stdout, "<pre>\n");
	fprintf(stdout, "%s", STRBUF(mbuf));
	fprintf(stdout, "</pre>\n");

	if (!internalcert) {
		/* The "mail" utility uses REPLYTO environment */
		replytoenv = (char *)malloc(strlen("REPLYTO=") + strlen(mailaddr) + 1);
		sprintf(replytoenv, "REPLYTO=%s", email);
		putenv(replytoenv);

		sprintf(cmd, "%s \"Certrequest %s\" '%s'", xgetenv("MAIL"), cn, mailaddr);
		pfd = popen(cmd, "w");
		fprintf(pfd, "%s", STRBUF(mbuf));
		if (fclose(pfd) == 0) {
			fprintf(stdout, "Request submitted OK<br>");
		}
		else {
			fprintf(stdout, "Request FAILED - could not send mail<br>");
		}
	}

	headfoot(stdout, "webcert", "", "footer", COL_BLUE);

	return 0;
}

