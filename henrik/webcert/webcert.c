/*----------------------------------------------------------------------------*/
/* Hobbit webpage generator tool.                                             */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: webcert.c,v 1.1 2009/01/26 09:34:16 henrik Exp hstoerne $";

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>

#include "libxymon.h"

char *certdir = "/var/ca/requests";
char *opensslcnf = "/var/ca/openssl.cnf";
char *rootcert = "/var/ca/private/CAcert.pem";
char *email = NULL;
char *corpid = NULL;
char *adminrealm = "CERTMGR";
char *phone = NULL;
char *costcode = NULL;
char *artemis = NULL;
char *validity = NULL;
char *servertype = NULL;
char *csrdata = NULL;
int  internalcert = 0;
enum { ADM_NONE, ADM_VIEWPENDING, ADM_MOVETOPROCESSING, ADM_VIEWPROCESSING, ADM_MOVETODONE, ADM_VIEWDONE, ADM_VIEWREQUEST } adminaction = ADM_NONE;
char *adminid = NULL;
char *hffile = "webcert";
char *hfform = "webcert_form";
int minkeysz = 2048;

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
		char *val = cwalk->value + strspn(cwalk->value, " \t");

		if (!val) { /* Do nothing */ }
		else if (strcmp(cwalk->name, "email") == 0) email = strdup(val);
		else if (strcmp(cwalk->name, "phone") == 0) phone = strdup(val);
		else if (strcmp(cwalk->name, "costcode") == 0) costcode = strdup(val);
		else if (strcmp(cwalk->name, "artemis") == 0) artemis = strdup(val);
		else if (strcmp(cwalk->name, "servertype") == 0) servertype = strdup(val);
		else if (strcmp(cwalk->name, "validity") == 0) validity = strdup(val);
		else if (strcmp(cwalk->name, "csrtext") == 0) csrdata = strdup(val);
		else if (strcmp(cwalk->name, "internalcert") == 0) internalcert = 1;
		else if (strcmp(cwalk->name, "corpid") == 0) corpid = strdup(val);
		else if (strcmp(cwalk->name, "viewpending") == 0) adminaction = ADM_VIEWPENDING;
		else if (strcmp(cwalk->name, "viewprocessing") == 0) adminaction = ADM_VIEWPROCESSING;
		else if (strcmp(cwalk->name, "viewdone") == 0) adminaction = ADM_VIEWDONE;
		else if (strcmp(cwalk->name, "viewrequest") == 0) {
			adminaction = ADM_VIEWREQUEST;
			adminid = strdup(val);
		}
		else if (strcmp(cwalk->name, "movetoprocessing") == 0) {
			adminaction = ADM_MOVETOPROCESSING;
			adminid = strdup(val);
		}
		else if (strcmp(cwalk->name, "movetodone") == 0) {
			adminaction = ADM_MOVETODONE;
			adminid = strdup(val);
		}

		cwalk = cwalk->next;
	}
}


void showreqlist(char *dir, char *title, char *action, char *actiontitle)
{
	DIR *d;
	struct dirent *de;
	char dirname[PATH_MAX];
	char *bgcols[] = { "#333333", "#000033", NULL };
	int idx = 0;

	if ((strcmp(dir, "new") != 0) && (strcmp(dir, "cur") != 0) && (strcmp(dir, "done") != 0)) return;

	sprintf(dirname, "%s/%s", certdir, dir);
	d = opendir(dirname);
	if (d == NULL) return;

	chdir(dirname);

	headfoot(stdout, hffile, "", "header", COL_BLUE);
	fprintf(stdout, "<center>\n");
	fprintf(stdout, "<table>\n");

	fprintf(stdout, "<h3>%s</h3>\n", title);
	while ((de = readdir(d)) != NULL) {
		struct stat st;
		char timestamp[20];

		if (*de->d_name == '.') continue;
		stat(de->d_name, &st);
		if (!S_ISREG(st.st_mode)) continue;

		strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
		fprintf(stdout, "<tr BGCOLOR=\"%s\"><td><a href=\"%s?viewrequest=%s/%s\">%s</a></td><td width=\"10px\">&nbsp;</td><td>%s</td>",
			bgcols[idx], getenv("SCRIPT_NAME"), dir, de->d_name, de->d_name, timestamp);

		if (action) {
			fprintf(stdout, "<td width=\"10px\">&nbsp;</td><td><a href=\"%s?%s=%s\">%s</a></td>",
				getenv("SCRIPT_NAME"), action, de->d_name, actiontitle);
		}

		fprintf(stdout, "<tr>\n");

		idx++; if (bgcols[idx] == NULL) idx = 0;
	}

	fprintf(stdout, "</table>\n");
	fprintf(stdout, "</center>\n");
	headfoot(stdout, hffile, "", "footer", COL_BLUE);
}


void showrequest(char *id)
{
	char fn[PATH_MAX];
	FILE *fd;
	char buf[4096];
	int n;
	char *d;
	
	strncpy(buf, id, sizeof(buf));
	d = dirname(buf);
	if ((strcmp(d, "new") != 0) && (strcmp(d, "cur") != 0) && (strcmp(d, "done") != 0)) return;

	sprintf(fn, "%s/%s/%s", certdir, d, basename(id));
	fd = fopen(fn, "r");
	if (fd == NULL) return;

	headfoot(stdout, hffile, "", "header", COL_BLUE);
	fprintf(stdout, "<pre>\n");

	while ((n = fread(buf, 1, sizeof(buf), fd)) > 0) fwrite(buf, 1, n, stdout);
	fclose(fd);

	fprintf(stdout, "</pre>\n");
	headfoot(stdout, hffile, "", "footer", COL_BLUE);
}


void moverequest(char *olddir, char *newdir, char *adminid, char *logcomment)
{
	char oldfn[PATH_MAX], newfn[PATH_MAX];
	FILE *fd;
	time_t now = getcurrenttime(NULL);
	char timestamp[30];

	sprintf(oldfn, "%s/%s/%s", certdir, olddir, adminid);
	sprintf(newfn, "%s/%s/%s", certdir, newdir, adminid);
	rename(oldfn, newfn);

	fd = fopen(newfn, "a");
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
	fprintf(fd, "%s: %s\n", logcomment, timestamp);
	fclose(fd);
}


int checkaccess(void)
{
	if (adminrealm == NULL) return 1; /* No access control */

	if ((getenv("REMOTE_USER") == NULL) || (strcmp(getenv("REMOTE_USER"), "anon") == 0)) {
		fprintf(stdout, "Status: 401 Unauthorized\n");
		fprintf(stdout, "WWW-authenticate: Basic realm=\"%s\"\n", adminrealm);
		fprintf(stdout, "Content-type: text/plain\n\n");
		fprintf(stdout, "Authorization required.\nUse login \"anon\" (no password) to request a certificate.\n");
		return 0;
	}

	return 1;
}

int main(int argc, char *argv[])
{
	int argi;
	char *envarea = NULL;
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
	int keysz = 0;

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
		else if (argnmatch(argv[argi], "--certdir=")) {
			char *p = strchr(argv[argi], '=');
			certdir = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--rootcert=")) {
			char *p = strchr(argv[argi], '=');
			rootcert = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--minimum-keysize=")) {
			char *p = strchr(argv[argi], '=');
			minkeysz = atoi(p+1);
		}
		else if (argnmatch(argv[argi], "--realm=")) {
			char *p = strchr(argv[argi], '=');
			adminrealm = strdup(p+1);
		}
		else if (strcmp(argv[argi], "--noauth") == 0) {
			adminrealm = NULL;
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


	if (cgi_method == CGI_GET) {
		switch (adminaction) {
		  case ADM_NONE:
			fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			showform(stdout, hffile, hfform, bgcolor, 0, NULL, NULL);
			break;

		  case ADM_VIEWPENDING:
			if (!checkaccess()) return 0;
			fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			showreqlist("new", "Pending requests", "movetoprocessing", "Set ordered");
			break;

		  case ADM_VIEWPROCESSING:
			if (!checkaccess()) return 0;
			fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			showreqlist("cur", "Requests being processed", "movetodone", "Set completed");
			break;

		  case ADM_VIEWDONE:
			if (!checkaccess()) return 0;
			fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			showreqlist("done", "Completed requests", NULL, NULL);
			break;

		  case ADM_VIEWREQUEST:
			if (!checkaccess()) return 0;
			fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			showrequest(adminid);
			break;

		  case ADM_MOVETOPROCESSING:
			if (!checkaccess()) return 0;
			fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			moverequest("new", "cur", adminid, "Ordered");
			showreqlist("new", "Pending requests", "movetoprocessing", "Set ordered");
			break;

		  case ADM_MOVETODONE:
			if (!checkaccess()) return 0;
			fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
			moverequest("cur", "done", adminid, "Completed");
			showreqlist("cur", "Requests being processed", "movetodone", "Set completed");
			break;
		}

		return 0;
	}

	fprintf(stdout, "Content-type: %s\n\n", xgetenv("HTMLCONTENTTYPE"));
	if (internalcert) {
		email = "none@sslcert.xymon.com";
		phone = "0";
		costcode = "0";
		artemis = "";
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
	if (!artemis) artemis = "";
	if (!servertype) addtobuffer(errortxt, "Servertype missing!<br>");
	if (!validity || (atoi(validity) <= 0)) addtobuffer(errortxt, "Validity invalid!<br>");
	if (!corpid || (*corpid == '\0')) addtobuffer(errortxt, "Company ID missing!<br>");
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
			char *subjstr = strstr(buf, "Subject:");
			char *keyszstr = strstr(buf, "RSA Public Key:");
			char *p;
	
			if (subjstr) {
				char *tok, *subjcopy;

				tok = subjstr;
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

			if (keyszstr) {
				char *tok;

				tok = keyszstr + strlen("RSA Public Key:");
				tok += strcspn(tok, "0123456678");
				keysz = atoi(tok);
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
		else if (keysz < minkeysz) {
			char txt[1024];
			sprintf(txt, "Invalid CSR: Detected a %d-bit public key, minimum required is %d-bit!<br>", keysz, minkeysz);
			addtobuffer(errortxt, txt);
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
		showform(stdout, hffile, hfform, bgcolor, 0, STRBUF(msg), NULL);
		return 0;
	}


	if (internalcert) {
		char certfn[PATH_MAX];

		sprintf(certfn, "/tmp/cert.%d", getpid());
		sprintf(cmd, "(echo y; echo y) | openssl ca -policy policy_anything -out %s -config %s -days %d -infiles %s", 
			certfn, opensslcnf, atoi(validity)*365, csrfn );
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
				fd = fopen(rootcert, "r");
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
		sprintf(buf, "Company ID: %s\n", corpid); addtobuffer(mbuf, buf);
		sprintf(buf, "Raw CSR:\n%s\n", csrdata); addtobuffer(mbuf, buf);
		addtobuffer(mbuf, "Parsed CSR info:\n");
		addtobuffer(mbuf, subj);
		addtobuffer(mbuf, "\n");
		sprintf(buf, "Common name: %s\n", cn); addtobuffer(mbuf, buf);
		sprintf(buf, "WHOIS info for domain %s\n", domainname); addtobuffer(mbuf, buf);
		addtostrbuffer(mbuf, whoisbuf);
		addtobuffer(mbuf, "\n");
	}


	headfoot(stdout, hffile, "", "header", COL_BLUE);

	fprintf(stdout, "<pre>\n");
	fprintf(stdout, "%s", STRBUF(mbuf));
	fprintf(stdout, "</pre>\n");

	if (!internalcert) {
		FILE *fd;
		char fn[PATH_MAX];
		char timestamp[30];
		time_t now = getcurrenttime(NULL);

		/* The "mail" utility uses REPLYTO environment */
		replytoenv = (char *)malloc(strlen("REPLYTO=") + strlen(email) + 1);
		sprintf(replytoenv, "REPLYTO=%s", email);
		putenv(replytoenv);

		sprintf(cmd, "%s \"Certrequest %s\" '%s'", xgetenv("MAIL"), cn, mailaddr);
		pfd = popen(cmd, "w");
		if (pfd != NULL) {
			fprintf(pfd, "%s", STRBUF(mbuf));
			if (fclose(pfd) == 0) {
				fprintf(stdout, "Request submitted OK<br>");
			}
			else {
				fprintf(stdout, "Request FAILED - could not send mail<br>");
			}
		}
		else {
			fprintf(stdout, "Could not submit request - please contact %s\n", mailaddr);
		}

		/* Save the request in the "new requests folder" */
		sprintf(fn, "%s/new/%s", certdir, cn);
		fd = fopen(fn, "w");
		if (fd) {
			fprintf(fd, "%s", STRBUF(mbuf));

			strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
			fprintf(fd, "\n\nOrdertime: %s\n", timestamp);
			fclose(fd);
		}
		else {
			errprintf("Cannot create file %s: %s\n", fn, strerror(errno));
		}
	}

	headfoot(stdout, hffile, "", "footer", COL_BLUE);

	return 0;
}

