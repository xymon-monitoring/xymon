/*----------------------------------------------------------------------------*/
/* Xymon monitor library.                                                     */
/*                                                                            */
/* This is a library module for Xymon, implementing IP-address based access   */
/* controls.                                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id$";

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>

#include "libxymon.h"

/*
 * The address bytes and family of a socket address. An IPv4-mapped IPv6
 * address (::ffff:192.0.2.1) is taken as the IPv4 address it carries, so a
 * dual-stack socket's IPv4 client matches IPv4 entries. Returns 0 for any
 * other family.
 */
static int sockaddr_bytes(struct sockaddr *sa, unsigned char *addr)
{
	if (sa->sa_family == AF_INET) {
		memcpy(addr, &((struct sockaddr_in *)sa)->sin_addr, 4);
		return AF_INET;
	}
	if (sa->sa_family == AF_INET6) {
		struct in6_addr *a6 = &((struct sockaddr_in6 *)sa)->sin6_addr;

		if (IN6_IS_ADDR_V4MAPPED(a6)) {
			memcpy(addr, ((unsigned char *)a6) + 12, 4);
			return AF_INET;
		}
		memcpy(addr, a6, 16);
		return AF_INET6;
	}
	return 0;
}

/* True when the first bits of a and b agree */
static int prefix_match(unsigned char *a, unsigned char *b, int bits)
{
	int full = bits / 8, rest = bits % 8;

	if (memcmp(a, b, full) != 0) return 0;
	if (rest == 0) return 1;
	return ((a[full] ^ b[full]) & (0xFF << (8 - rest)) & 0xFF) == 0;
}

/* An address as text, IPv4 or IPv6, into buf -- for messages and logs */
char *sockaddr_text(struct sockaddr *sa, char *buf, size_t buflen)
{
	unsigned char addr[16];
	int family = sockaddr_bytes(sa, addr);

	if ((family == 0) || (inet_ntop(family, addr, buf, buflen) == NULL)) snprintf(buf, buflen, "?");
	return buf;
}

/*
 * Whether a and b come from the same sender: the same IPv4 address, or the
 * same IPv6 /64. One machine is usually given a whole /64, so counting single
 * IPv6 addresses would let it pass for millions of senders.
 */
int sockaddr_same_sender(struct sockaddr *a, struct sockaddr *b)
{
	unsigned char aa[16], ba[16];
	int family = sockaddr_bytes(a, aa);

	if ((family == 0) || (sockaddr_bytes(b, ba) != family)) return 0;
	return prefix_match(aa, ba, ((family == AF_INET) ? 32 : 64));
}

/* An address written as text, IPv4 or IPv6, into ss. Returns 1, or 0 if it is neither. */
int text_sockaddr(char *text, struct sockaddr_storage *ss)
{
	memset(ss, 0, sizeof(*ss));
	if (inet_pton(AF_INET, text, &((struct sockaddr_in *)ss)->sin_addr) == 1) {
		ss->ss_family = AF_INET;
		return 1;
	}
	if (inet_pton(AF_INET6, text, &((struct sockaddr_in6 *)ss)->sin6_addr) == 1) {
		ss->ss_family = AF_INET6;
		return 1;
	}
	ss->ss_family = AF_INET;	/* left as 0.0.0.0 */
	return 0;
}

/*
 * host_sockaddr -- NAME's address into SS, from the system resolver: its IPv4
 * address if it has one, else its IPv6 one, as the resolver gave it (an IPv6
 * scope kept), port 0. Returns 1, or 0 if it has neither, with the
 * getaddrinfo() error in *ERR when ERR is not NULL (0 on success).
 *
 * One family at a time, as gethostbyname() asked for IPv4 alone: a name with
 * an IPv4 address costs one query, and a resolver that drops IPv6 queries
 * delays only the names without one; a name with no address costs two. IPv6
 * is not asked after EAI_AGAIN: a timeout, which it would only repeat, as
 * xymonnet's c-ares lookup does not after one, but on glibc and musl also a
 * server failure (SERVFAIL), so a name whose IPv4 question fails that way is
 * not resolved even if it has an IPv6 address, as gethostbyname() did not.
 * After any other failure *ERR is the IPv6 question's error. glibc answers
 * an IPv4 question for a name /etc/hosts gives to ::1 alone with 127.0.0.1,
 * as gethostbyname() did.
 *
 * An IPv4 address inet_aton() reads, short forms such as 127.1 included, is
 * taken as it is first: OpenBSD's getaddrinfo() reads only the dotted quad,
 * and the callers read addresses with inet_aton() before.
 *
 * A scopeless IPv6 literal inet_pton() reads is taken first too, as the IPv4
 * one is: the resolver must not be asked for a numeric address, and a BSD
 * without one answers the bogus IPv4 question for a literal such as ::1 in a
 * way that ends the loop before its IPv6 question. A scoped literal
 * (fe80::1%eth0) is left to the resolver, which keeps its scope in the result;
 * inet_pton() fills only the address, having no field for the scope to go in.
 */
int host_sockaddr(char *name, struct sockaddr_storage *ss, int *err)
{
	static const int families[] = { AF_INET, AF_INET6 };
	struct addrinfo hints, *res;
	struct in_addr a4;
	struct in6_addr a6;
	int i, rc = EAI_NONAME;

	if (inet_aton(name, &a4)) {
		memset(ss, 0, sizeof(*ss));
		ss->ss_family = AF_INET;
		((struct sockaddr_in *)ss)->sin_addr = a4;
		if (err) *err = 0;
		return 1;
	}

	if ((strchr(name, ':') != NULL) && (strchr(name, '%') == NULL) && (inet_pton(AF_INET6, name, &a6) == 1)) {
		memset(ss, 0, sizeof(*ss));
		ss->ss_family = AF_INET6;
		((struct sockaddr_in6 *)ss)->sin6_addr = a6;
		if (err) *err = 0;
		return 1;
	}

	memset(ss, 0, sizeof(*ss));
	for (i = 0; i < 2; i++) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = families[i];
		hints.ai_socktype = SOCK_STREAM;
		rc = getaddrinfo(name, NULL, &hints, &res);
		if ((rc == 0) && res && (res->ai_family == families[i]) && (res->ai_addrlen <= sizeof(*ss))) {
			memcpy(ss, res->ai_addr, res->ai_addrlen);
			freeaddrinfo(res);
			if (err) *err = 0;
			return 1;
		}
		if (rc == 0) { if (res) freeaddrinfo(res); rc = EAI_NONAME; }
		if (rc == EAI_AGAIN) break;
	}
	if (err) *err = rc;

	return 0;
}

/*
 * host_address -- host_sockaddr()'s address as text in BUF, or NULL. The text
 * has no IPv6 scope: to connect to a link-local address, use host_sockaddr().
 */
char *host_address(char *name, char *buf, size_t buflen, int *err)
{
	struct sockaddr_storage ss;

	if (!host_sockaddr(name, &ss, err)) return NULL;
	return sockaddr_text((struct sockaddr *)&ss, buf, buflen);
}

/*
 * A comma-separated list of addresses or networks, IPv4 or IPv6, each
 * optionally with a prefix length: "192.0.2.0/24,2001:db8::/32,::1".
 * An entry that is neither address is left out, with an error.
 */
sender_t *getsenderlist(char *iplist)
{
	char *ips, *p, *tok;
	sender_t *result;
	int count;

	dbgprintf("-> getsenderlist\n");

	ips = strdup(iplist);
	count = 0; p = ips; do { count++; p = strchr(p, ','); if (p) p++; } while (p);
	result = (sender_t *) calloc(1, sizeof(sender_t) * (count+1));

	tok = strtok(ips, ","); count = 0;
	while (tok) {
		int maxbits;

		p = strchr(tok, '/');
		if (p) *p = '\0';
		if (inet_pton(AF_INET, tok, result[count].addr) == 1) {
			result[count].family = AF_INET; maxbits = 32;
		}
		else if (inet_pton(AF_INET6, tok, result[count].addr) == 1) {
			result[count].family = AF_INET6; maxbits = 128;
		}
		else {
			errprintf("Ignoring sender address '%s': not an IPv4 or IPv6 address\n", tok);
			tok = strtok(NULL, ",");
			continue;
		}
		result[count].bits = maxbits;
		if (p) {
			int bits = atoi(p+1);
			if ((bits >= 0) && (bits < maxbits)) result[count].bits = bits;
		}

		tok = strtok(NULL, ",");
		count++;
	}

	xfree(ips);
	dbgprintf("<- getsenderlist\n");

	return result;
}

/* True when sa falls in one of list's networks, of its own family */
int sender_in_list(sender_t *list, struct sockaddr *sa)
{
	unsigned char addr[16];
	int family = sockaddr_bytes(sa, addr);
	int i;

	if (!list || (family == 0)) return 0;
	for (i = 0; (list[i].family != 0); i++) {
		if ((list[i].family == family) && prefix_match(list[i].addr, addr, list[i].bits)) return 1;
	}
	return 0;
}

/*
 * May the sender at sa send this message? Yes when there is no list; when
 * the message is about targetip and comes from that address (a host may
 * report on itself, and "0.0.0.0" -- a DHCP host -- from anywhere); when
 * it came through the backfeed channel, which xymond marks with the IPv4
 * address 0.0.0.0; or when the list allows sa. Only an IPv4 0.0.0.0 is
 * the backfeed: an IPv6 sender is never mistaken for it.
 */
int oksender_addr(sender_t *oklist, char *targetip, struct sockaddr *sa, char *msgbuf)
{
	unsigned char addr[16];
	int family;
	char *eoln = NULL;
	char text[IP_ADDR_STRLEN];

	dbgprintf("-> oksender\n");

	/* If oklist is empty, we're not doing any access checks - so return OK */
	if (oklist == NULL) {
		dbgprintf("<- oksender(1-a)\n");
		return 1;
	}

	family = sockaddr_bytes(sa, addr);

	/* If we know the target, it would be ok for the host to report on itself. */
	if (targetip) {
		struct sockaddr_storage tg;
		unsigned char tgaddr[16];

		if (strcmp(targetip, "0.0.0.0") == 0) return 1; /* DHCP hosts can report from any address */
		if (text_sockaddr(targetip, &tg) && (family != 0) &&
		    (sockaddr_bytes((struct sockaddr *)&tg, tgaddr) == family) &&
		    (memcmp(tgaddr, addr, (family == AF_INET) ? 4 : 16) == 0)) {
			dbgprintf("<- oksender(1-b)\n");
			return 1;
		}
	}

	/* If sender is 0.0.0.0 (i.e. it arrived via backfeed channel), then OK */
	if ((family == AF_INET) && (memcmp(addr, "\0\0\0\0", 4) == 0)) {
		dbgprintf("<- oksender(1-c)\n");
		return 1;
	}

	/* It's someone else reporting about the host. Check the access list */
	if (sender_in_list(oklist, sa)) {
		dbgprintf("<- oksender(1-d)\n");
		return 1;
	}

	/* Refuse and log the message */
	if (msgbuf) { eoln = strchr(msgbuf, '\n'); if (eoln) *eoln = '\0'; }
	errprintf("Refused message from %s: %s\n", sockaddr_text(sa, text, sizeof(text)), (msgbuf ? msgbuf : ""));
	if (msgbuf && eoln) *eoln = '\n';

	dbgprintf("<- oksender(0)\n");

	return 0;
}

/* oksender_addr() for an IPv4 sender, as msgcache has one */
int oksender(sender_t *oklist, char *targetip, struct in_addr sender, char *msgbuf)
{
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr = sender;
	return oksender_addr(oklist, targetip, (struct sockaddr *)&sin, msgbuf);
}
