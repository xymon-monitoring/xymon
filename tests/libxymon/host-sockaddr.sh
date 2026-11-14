#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# tests/libxymon/host-sockaddr.sh
#
# host_sockaddr() turns a name or an address into the address to use:
#
#   - an IPv4 address inet_aton() reads, short forms such as 127.1 included,
#     without asking the resolver: OpenBSD's getaddrinfo() reads only the
#     dotted quad, and the callers read addresses with inet_aton() before;
#   - a name's IPv4 address if it has one, else its IPv6 one;
#   - one family at a time, IPv4 first, as gethostbyname() asked for IPv4
#     alone: a resolver that drops IPv6 queries then delays only the names
#     without an IPv4 address;
#   - after a temporary failure (EAI_AGAIN) no IPv6 question, and that error
#     in *err; after no address at all, the resolver's error in *err.
#
# The harness brings its own getaddrinfo() and freeaddrinfo(), linked before
# libc so they are the ones host_sockaddr() calls. They know four names, each
# answering per family, and count the questions asked for each family.

set -euo pipefail
# shellcheck source=tests/lib/assert.sh
. "$(dirname "$0")/../lib/assert.sh"

ROOT=$(find_root)
require_c_buildenv "$ROOT"
work=$(mktempdir)
build_xymon_libs "$ROOT" "$work/libbuild.log" libxymoncomm.a

cat >"$work/harness.c" <<'HARNESS'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "libxymon.h"

static int asked4, asked6, askedboth;

/* An answer of one address of FAMILY */
static struct addrinfo *answer(int family, const char *text)
{
	struct addrinfo *ai = calloc(1, sizeof(*ai));
	struct sockaddr_storage *ss = calloc(1, sizeof(*ss));

	ai->ai_family = family;
	ai->ai_socktype = SOCK_STREAM;
	ai->ai_addr = (struct sockaddr *)ss;
	ss->ss_family = family;
	if (family == AF_INET) {
		inet_pton(AF_INET, text, &((struct sockaddr_in *)ss)->sin_addr);
		ai->ai_addrlen = sizeof(struct sockaddr_in);
	}
	else {
		inet_pton(AF_INET6, text, &((struct sockaddr_in6 *)ss)->sin6_addr);
		ai->ai_addrlen = sizeof(struct sockaddr_in6);
	}
	return ai;
}

/*
 * both.test has an address of each family, v6only.test an IPv6 one alone,
 * again.test fails for now on IPv4, none.test has none, empty.test answers
 * "success" with no address for either family, odd.test answers the IPv4
 * question with an IPv6 address alone; any other name is unknown.
 */
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	int family = hints ? hints->ai_family : AF_UNSPEC;

	(void)service;
	*res = NULL;
	if (family == AF_INET) asked4++;
	else if (family == AF_INET6) asked6++;
	else askedboth++;

	if (strcmp(node, "both.test") == 0) {
		if (family == AF_INET6) { *res = answer(AF_INET6, "2001:db8::4"); return 0; }
		*res = answer(AF_INET, "192.0.2.4");
		if (family == AF_UNSPEC) (*res)->ai_next = answer(AF_INET6, "2001:db8::4");
		return 0;
	}
	if (strcmp(node, "v6only.test") == 0) {
		if (family == AF_INET) return EAI_NONAME;
		*res = answer(AF_INET6, "2001:db8::6");
		return 0;
	}
	if (strcmp(node, "empty.test") == 0) return 0;
	if (strcmp(node, "odd.test") == 0) { *res = answer(AF_INET6, "2001:db8::9"); return 0; }
	if (strcmp(node, "again.test") == 0) {
		if (family == AF_INET6) { *res = answer(AF_INET6, "2001:db8::7"); return 0; }
		return EAI_AGAIN;
	}
	return EAI_NONAME;
}

void freeaddrinfo(struct addrinfo *res)
{
	while (res) {
		struct addrinfo *next = res->ai_next;

		free(res->ai_addr);
		free(res);
		res = next;
	}
}

/* NAME as host_sockaddr() gives it: "ADDRESS" or "- ERRORCODE", and the questions asked */
static int check(char *name, const char *want, int want4, int want6)
{
	struct sockaddr_storage ss;
	char got[100], text[INET6_ADDRSTRLEN] = "?";
	int err = -12345;

	asked4 = asked6 = askedboth = 0;
	if (host_sockaddr(name, &ss, &err)) {
		if (ss.ss_family == AF_INET) inet_ntop(AF_INET, &((struct sockaddr_in *)&ss)->sin_addr, text, sizeof(text));
		else if (ss.ss_family == AF_INET6) inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&ss)->sin6_addr, text, sizeof(text));
		snprintf(got, sizeof(got), "%s err %d", text, err);
	}
	else {
		snprintf(got, sizeof(got), "- err %s", (err == EAI_AGAIN) ? "EAI_AGAIN" : (err == EAI_NONAME) ? "EAI_NONAME" : "other");
	}
	if ((strcmp(got, want) == 0) && (asked4 == want4) && (asked6 == want6) && (askedboth == 0)) return 0;
	printf("host_sockaddr(\"%s\") = %s, asked IPv4 %d, IPv6 %d, both at once %d; want %s, IPv4 %d, IPv6 %d\n",
	       name, got, asked4, asked6, askedboth, want, want4, want6);
	return 1;
}

int main(void)
{
	int fails = 0;

	/* Addresses: no question */
	fails += check("127.1", "127.0.0.1 err 0", 0, 0);
	fails += check("10.1.2.3", "10.1.2.3 err 0", 0, 0);
	/* Names: IPv4 first, IPv6 only when IPv4 has none */
	fails += check("both.test", "192.0.2.4 err 0", 1, 0);
	fails += check("v6only.test", "2001:db8::6 err 0", 1, 1);
	/* A temporary failure: no IPv6 question, and the error kept */
	fails += check("again.test", "- err EAI_AGAIN", 1, 0);
	fails += check("none.test", "- err EAI_NONAME", 1, 1);
	/* "Success" with nothing in it is no address */
	fails += check("empty.test", "- err EAI_NONAME", 1, 1);
	/* An answer of the other family is not the IPv4 address asked for */
	fails += check("odd.test", "2001:db8::9 err 0", 1, 1);
	return fails;
}
HARNESS
# shellcheck disable=SC2046  # the flag lists are meant to split
"$CC" $(xymon_cflags "$ROOT") -o "$work/harness" "$work/harness.c" \
	"$(xymon_lib "$ROOT" libxymoncomm.a)" $(xymon_ldflags "$ROOT") 2>"$work/cc.log" \
	|| fail "the harness does not compile: $(cat "$work/cc.log")"
out=$("$work/harness" 2>&1) || fail "$out"

pass "host_sockaddr() reads an IPv4 address, short forms included, without the resolver, and asks for a name's IPv4 address, then its IPv6 one, one family at a time"
