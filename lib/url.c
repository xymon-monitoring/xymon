/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for URL parsing and mangling.                         */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: url.c,v 1.19 2006-05-03 21:12:33 henrik Exp $";

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/param.h>
#include <netdb.h>

#include "libbbgen.h"

/* This is used for loading a .netrc file with hostnames and authentication tokens. */
typedef struct loginlist_t {
	char *host;
	char *auth;
	struct loginlist_t *next;
} loginlist_t;

static loginlist_t *loginhead = NULL;


/*
 * Convert a URL with "%XX" hexadecimal escaped style bytes into normal form.
 * Result length will always be <= source length.
 */
char *urlunescape(char *url)
{
	static char *result = NULL;
	char *pin, *pout;

	pin = url;
	if (result) xfree(result);
	pout = result = (char *) malloc(strlen(url) + 1);
	while (*pin) {
		if (*pin == '+') {
			*pout = ' ';
			pin++;
		}
		else if (*pin == '%') {
			pin++;
			if ((strlen(pin) >= 2) && isxdigit((int)*pin) && isxdigit((int)*(pin+1))) {
				*pout = 16*hexvalue(*pin) + hexvalue(*(pin+1));
				pin += 2;
			}
			else {
				*pout = '%';
				pin++;
			}
		}
		else {
			*pout = *pin;
			pin++;
		}

		pout++;
	}

	*pout = '\0';

	return result;
}

/*
 * Get an environment variable (eg: QUERY_STRING) and do CGI decoding of it.
 */
char *urldecode(char *envvar)
{
	if (xgetenv(envvar) == NULL) return NULL;

	return urlunescape(xgetenv(envvar));
}

/*
 * Do a CGI encoding of a URL, i.e. unusual chars are converted to %XX.
 */
char *urlencode(char *s)
{
	static char *result = NULL;
	static int resbufsz = 0;
	char *inp, *outp;

	if (result == NULL) {
		result = (char *)malloc(1024);
		resbufsz = 1024;
	}
	outp = result;

	for (inp = s; (*inp); inp++) {
		if ((outp - result) > (resbufsz - 5)) {
			int offset = (outp - result);

			resbufsz += 1024;
			result = (char *)realloc(result, resbufsz);
			outp = result + offset;
		}

		if ( ( (*inp >= 'a') && (*inp <= 'z') ) ||
		     ( (*inp >= 'A') && (*inp <= 'Z') ) ||
		     ( (*inp >= '0') && (*inp <= '9') ) ) {
			*outp = *inp;
			outp++;
		}
		else {
			sprintf(outp, "%%%0x", *inp);
			outp += 3;
		}
	}

	*outp = '\0';
	return result;
}

/*
 * Check if a URL contains only safe characters.
 * This is not really needed any more, since there are no more CGI
 * shell-scripts that directly process the QUERY_STRING parameter.
 */
int urlvalidate(char *query, char *validchars)
{
#if 0
	static int valid;
	char *p;

	if (validchars == NULL) validchars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ,.-:&_%=*+/ ";

	for (p=query, valid=1; (valid && *p); p++) {
		valid = (strchr(validchars, *p) != NULL);
	}

	return valid;
#else
	return 1;
#endif
}

/*
 * Load the $HOME/.netrc file with authentication tokens for HTTP tests.
 */
static void load_netrc(void)
{

#define WANT_TOKEN   0
#define MACHINEVAL   1
#define LOGINVAL     2
#define PASSVAL      3
#define OTHERVAL     4

	static int loaded = 0;

	char netrcfn[MAXPATHLEN];
	FILE *fd;
	strbuffer_t *inbuf;
	char *host, *login, *password, *p;
	int state = WANT_TOKEN;

	if (loaded) return;
	loaded = 1;

	MEMDEFINE(netrcfn);

	/* Look for $BBHOME/etc/netrc first, then the default ~/.netrc */
	sprintf(netrcfn, "%s/etc/netrc", xgetenv("BBHOME"));
	fd = fopen(netrcfn, "r");
	/* Can HOME be undefined ? Yes, on Solaris when started during boot */
	if ((fd == NULL) && getenv("HOME")) {
		sprintf(netrcfn, "%s/.netrc", xgetenv("HOME"));
		fd = fopen(netrcfn, "r");
	}

	if (fd == NULL) {
		MEMUNDEFINE(netrcfn);
		return;
	}

	host = login = password = NULL;
	initfgets(fd);
	inbuf = newstrbuffer(0);
	while (unlimfgets(inbuf, fd)) {
		sanitize_input(inbuf, 0, 0);

		if (STRBUFLEN(inbuf) != 0) {
			p = strtok(STRBUF(inbuf), " \t");
			while (p) {
				switch (state) {
				  case WANT_TOKEN:
					if (strcmp(p, "machine") == 0) state = MACHINEVAL;
					else if (strcmp(p, "login") == 0) state = LOGINVAL;
					else if (strcmp(p, "password") == 0) state = PASSVAL;
					else if (strcmp(p, "account") == 0) state = OTHERVAL;
					else if (strcmp(p, "macdef") == 0) state = OTHERVAL;
					else if (strcmp(p, "default") == 0) { host = ""; state = WANT_TOKEN; }
					else state = WANT_TOKEN;
					break;

				  case MACHINEVAL:
					host = strdup(p); state = WANT_TOKEN; break;

				  case LOGINVAL:
					login = strdup(p); state = WANT_TOKEN; break;

				  case PASSVAL:
					password = strdup(p); state = WANT_TOKEN; break;

				  case OTHERVAL:
				  	state = WANT_TOKEN; break;
				}

				if (host && login && password) {
					loginlist_t *item = (loginlist_t *) malloc(sizeof(loginlist_t));

					item->host = host;
					item->auth = (char *) malloc(strlen(login) + strlen(password) + 2);
					sprintf(item->auth, "%s:%s", login, password);
					item->next = loginhead;
					loginhead = item;
					host = login = password = NULL;
				}

				p = strtok(NULL, " \t");
			}
		}
	}

	fclose(fd);
	freestrbuffer(inbuf);

	MEMUNDEFINE(netrcfn);
}

/*
 * Clean a URL of double-slashes.
 */
char *cleanurl(char *url)
{
	static char *cleaned = NULL;
	char *pin, *pout;
	int  lastwasslash = 0;

	if (cleaned == NULL)
		cleaned = (char *)malloc(strlen(url)+1);
	else {
		cleaned = (char *)realloc(cleaned, strlen(url)+1);
	}

	for (pin=url, pout=cleaned, lastwasslash=0; (*pin); pin++) {
		if (*pin == '/') {
			if (!lastwasslash) { 
				*pout = *pin; 
				pout++; 
			}
			lastwasslash = 1;
		}
		else {
			*pout = *pin; 
			pout++;
			lastwasslash = 0;
		}
	}
	*pout = '\0';

	return cleaned;
}


/*
 * Parse a URL into components, following the guidelines in RFC 1808.
 * This fills out a urlelem_t struct with the elements, and also
 * constructs a canonical form of the URL.
 */
void parse_url(char *inputurl, urlelem_t *url)
{

	char *tempurl;
	char *fragment = NULL;
	char *netloc;
	char *startp, *p;
	int haveportspec = 0;
	char *canonurl;
	int canonurllen;

	memset(url, 0, sizeof(urlelem_t));
	url->scheme = url->host = url->relurl = "";

	/* Get a temp. buffer we can molest */
	tempurl = strdup(inputurl);

	/* First cut off any fragment specifier */
	fragment = strchr(tempurl, '#'); if (fragment) *fragment = '\0';

	/* Get the scheme (protocol) */
	startp = tempurl;
	p = strchr(startp, ':');
	if (p) {
		*p = '\0';
		if (strncmp(startp, "https", 5) == 0) {
			url->scheme = "https";
			url->port = 443;
			if (strlen(startp) > 5) url->schemeopts = strdup(startp+5);
		} else if (strncmp(startp, "http", 4) == 0) {
			url->scheme = "http";
			url->port = 80;
			if (strlen(startp) > 4) url->schemeopts = strdup(startp+4);
		} else if (strcmp(startp, "ftp") == 0) {
			url->scheme = "ftp";
			url->port = 21;
		} else if (strcmp(startp, "ldap") == 0) {
			url->scheme = "ldap";
			url->port = 389;
		} else if (strcmp(startp, "ldaps") == 0) {
			url->scheme = "ldaps";
			url->port = 389; /* ldaps:// URL's are non-standard, and must use port 389+STARTTLS */
		}
		else {
			/* Unknown scheme! */
			errprintf("Unknown URL scheme '%s' in URL '%s'\n", startp, inputurl);
			url->scheme = strdup(startp);
			url->port = 0;
		}
		startp = (p+1);
	}
	else {
		errprintf("Malformed URL - no 'scheme:' in '%s'\n", inputurl);
		url->parseerror = 1;
		return;
	}

	if (strncmp(startp, "//", 2) == 0) {
		startp += 2;
		netloc = startp;

		p = strchr(startp, '/');
		if (p) {
			*p = '\0';
			startp = (p+1);
		}
		else startp += strlen(startp);
	}
	else {
		errprintf("Malformed URL missing '//' in '%s'\n", inputurl);
		url->parseerror = 2;
		return;
	}

	/* netloc is [username:password@]hostname[:port][=forcedIP] */
	p = strchr(netloc, '@');
	if (p) {
		*p = '\0';
		url->auth = strdup(urlunescape(netloc));
		netloc = (p+1);
	}
	p = strchr(netloc, '=');
	if (p) {
		url->ip = strdup(p+1);
		*p = '\0';
	}
	p = strchr(netloc, ':');
	if (p) {
		haveportspec = 1;
		*p = '\0';
		url->port = atoi(p+1);
	}

	url->host = strdup(netloc);
	if (url->port == 0) {
		struct servent *svc = getservbyname(url->scheme, NULL);
		if (svc) url->port = ntohs(svc->s_port);
		else {
			errprintf("Unknown scheme (no port) '%s'\n", url->scheme);
			url->parseerror = 3;
			return;
		}
	}

	if (fragment) *fragment = '#';
	url->relurl = malloc(strlen(startp) + 2);
	sprintf(url->relurl, "/%s", startp);

	if (url->auth == NULL) {
		/* See if we have it in the .netrc list */
		loginlist_t *walk;

		load_netrc();
		for (walk = loginhead; (walk && (strcmp(walk->host, url->host) != 0)); walk = walk->next) ;
		if (walk) url->auth = walk->auth;
	}

	/* Build the canonical form of this URL, free from all BB'isms */
	canonurllen = 1;
	canonurllen += strlen(url->scheme)+3;	/* Add room for the "://" */
	canonurllen += strlen(url->host);
	canonurllen += 6; 			/* Max. length of a port spec. */
	canonurllen += strlen(url->relurl);

	p = canonurl = (char *)malloc(canonurllen);
	p += sprintf(p, "%s://", url->scheme);
	/*
	 * Dont include authentication here, since it 
	 * may show up in clear text on the info page.
	 * And it is not used in URLs to access the site.
	 * if (url->auth) p += sprintf(p, "%s@", url->auth);
	 */
	p += sprintf(p, "%s", url->host);
	if (haveportspec) p += sprintf(p, ":%d", url->port);
	p += sprintf(p, "%s", url->relurl);
	url->origform = canonurl;

	xfree(tempurl);
	return;
}

/*
 * If a column name is column=NAME, pick out NAME.
 */
static char *gethttpcolumn(char *inp, char **name)
{
	char *nstart, *nend;

	nstart = inp;
	nend = strchr(nstart, ';');
	if (nend == NULL) {
		*name = NULL;
		return inp;
	}

	*nend = '\0';
	*name = strdup(nstart);
	*nend = ';';

	return nend+1;
}


/* 
 * Split a BB test-specification with a URL and optional 
 * post-data/expect-data/expect-type data into the URL itself 
 * and the other elements.
 * Un-escape data in the post- and expect-data.
 * Parse the URL.
 */
char *decode_url(char *testspec, bburl_t *bburl)
{
	static bburl_t bburlbuf;
	static urlelem_t desturlbuf, proxyurlbuf;

	char *inp, *p;
	char *urlstart, *poststart, *postcontenttype, *expstart, *proxystart, *okstart, *notokstart;
	urlstart = poststart = postcontenttype = expstart = proxystart = okstart = notokstart = NULL;

	/* If called with no buffer, use our own static one */
	if (bburl == NULL) {
		memset(&bburlbuf, 0, sizeof(bburl_t));
		memset(&desturlbuf, 0, sizeof(urlelem_t));
		memset(&proxyurlbuf, 0, sizeof(urlelem_t));

		bburl = &bburlbuf;
		bburl->desturl = &desturlbuf;
		bburl->proxyurl = NULL;
	}
	else {
		memset(bburl, 0, sizeof(bburl_t));
		bburl->desturl = (urlelem_t*) calloc(1, sizeof(urlelem_t));
		bburl->proxyurl = NULL;
	}

	inp = strdup(testspec);

	if (strncmp(inp, "content=", 8) == 0) {
		bburl->testtype = BBTEST_CONTENT;
		urlstart = inp+8;
	} else if (strncmp(inp, "cont;", 5) == 0) {
		bburl->testtype = BBTEST_CONT;
		urlstart = inp+5;
	} else if (strncmp(inp, "cont=", 5) == 0) {
		bburl->testtype = BBTEST_CONT;
		urlstart = gethttpcolumn(inp+5, &bburl->columnname);
	} else if (strncmp(inp, "nocont;", 7) == 0) {
		bburl->testtype = BBTEST_NOCONT;
		urlstart = inp+7;
	} else if (strncmp(inp, "nocont=", 7) == 0) {
		bburl->testtype = BBTEST_NOCONT;
		urlstart = gethttpcolumn(inp+7, &bburl->columnname);
	} else if (strncmp(inp, "post;", 5) == 0) {
		bburl->testtype = BBTEST_POST;
		urlstart = inp+5;
	} else if (strncmp(inp, "post=", 5) == 0) {
		bburl->testtype = BBTEST_POST;
		urlstart = gethttpcolumn(inp+5, &bburl->columnname);
	} else if (strncmp(inp, "nopost;", 7) == 0) {
		bburl->testtype = BBTEST_NOPOST;
		urlstart = inp+7;
	} else if (strncmp(inp, "nopost=", 7) == 0) {
		bburl->testtype = BBTEST_NOPOST;
		urlstart = gethttpcolumn(inp+7, &bburl->columnname);
	} else if (strncmp(inp, "soap;", 5) == 0) {
		bburl->testtype = BBTEST_SOAP;
		urlstart = inp+5;
	} else if (strncmp(inp, "soap=", 5) == 0) {
		bburl->testtype = BBTEST_SOAP;
		urlstart = gethttpcolumn(inp+5, &bburl->columnname);
	} else if (strncmp(inp, "nosoap;", 7) == 0) {
		bburl->testtype = BBTEST_NOSOAP;
		urlstart = inp+7;
	} else if (strncmp(inp, "nosoap=", 7) == 0) {
		bburl->testtype = BBTEST_NOSOAP;
		urlstart = gethttpcolumn(inp+7, &bburl->columnname);
	} else if (strncmp(inp, "type;", 5) == 0) {
		bburl->testtype = BBTEST_TYPE;
		urlstart = inp+5;
	} else if (strncmp(inp, "type=", 5) == 0) {
		bburl->testtype = BBTEST_TYPE;
		urlstart = gethttpcolumn(inp+5, &bburl->columnname);
	} else if (strncmp(inp, "httpstatus;", 11) == 0) {
		bburl->testtype = BBTEST_STATUS;
		urlstart = strchr(inp, ';') + 1;
	} else if (strncmp(inp, "httpstatus=", 11) == 0) {
		bburl->testtype = BBTEST_STATUS;
		urlstart = gethttpcolumn(inp+11, &bburl->columnname);
	} else if (strncmp(inp, "http=", 5) == 0) {
		/* Plain URL test, but in separate column */
		bburl->testtype = BBTEST_PLAIN;
		urlstart = gethttpcolumn(inp+5, &bburl->columnname);
	} else {
		/* Plain URL test */
		bburl->testtype = BBTEST_PLAIN;
		urlstart = inp;
	}

	switch (bburl->testtype) {
	  case BBTEST_PLAIN:
		  break;

	  case BBTEST_CONT:
	  case BBTEST_NOCONT:
	  case BBTEST_TYPE:
		  expstart = strchr(urlstart, ';');
		  if (expstart) {
			  *expstart = '\0';
			  expstart++;
		  }
		  else {
			  errprintf("content-check, but no content-data in '%s'\n", testspec);
			  bburl->testtype = BBTEST_PLAIN;
		  }
		  break;

	  case BBTEST_POST:
	  case BBTEST_NOPOST:
	  case BBTEST_SOAP:
		  poststart = strchr(urlstart, ';');
		  if (poststart) {
			  *poststart = '\0';
			  poststart++;

			/* See if "poststart" points to a content-type */
			if (strncasecmp(poststart, "(content-type=", 14) == 0) {
				postcontenttype = poststart+14;
				poststart = strchr(postcontenttype, ')');
				if (poststart) {
					*poststart = '\0';
					poststart++;
				}
			}

			if (poststart) {
			  expstart = strchr(poststart, ';');
			  if (expstart) {
				  *expstart = '\0';
				  expstart++;
			  }
			}

			if ((bburl->testtype == BBTEST_NOPOST) && (!expstart)) {
			  		errprintf("content-check, but no content-data in '%s'\n", testspec);
			  		bburl->testtype = BBTEST_PLAIN;
				  }
			  }
		  else {
			  errprintf("post-check, but no post-data in '%s'\n", testspec);
			  bburl->testtype = BBTEST_PLAIN;
		  }
		  break;

	  case BBTEST_STATUS:
		okstart = strchr(urlstart, ';');
		if (okstart) {
			*okstart = '\0';
			okstart++;

			notokstart = strchr(okstart, ';');
			if (notokstart) {
				*notokstart = '\0';
				notokstart++;
			}
		}

		if (okstart && (strlen(okstart) == 0)) okstart = NULL;
		if (notokstart && (strlen(notokstart) == 0)) notokstart = NULL;

		if (!okstart && !notokstart) {
			errprintf("HTTP status check, but no OK/not-OK status codes in '%s'\n", testspec);
			bburl->testtype = BBTEST_PLAIN;
		}

		if (okstart) bburl->okcodes = strdup(okstart);
		if (notokstart) bburl->badcodes = strdup(notokstart);
	}

	if (poststart) getescapestring(poststart, &bburl->postdata, NULL);
	if (postcontenttype) getescapestring(postcontenttype, &bburl->postcontenttype, NULL);
	if (expstart)  getescapestring(expstart, &bburl->expdata, NULL);

	p = strstr(urlstart, "/http://");
	if (!p)
		p = strstr(urlstart, "/https://");
	if (p) {
		proxystart = urlstart;
		urlstart = (p+1);
		*p = '\0';
	}

	parse_url(urlstart, bburl->desturl);
	if (proxystart) {
		if (bburl == &bburlbuf) {
			/* We use our own static buffers */
			bburl->proxyurl = &proxyurlbuf;
		}
		else {
			/* User allocated buffers */
			bburl->proxyurl = (urlelem_t *)malloc(sizeof(urlelem_t));
		}

		parse_url(proxystart, bburl->proxyurl);
	}

	xfree(inp);

	return bburl->desturl->origform;
}
 
