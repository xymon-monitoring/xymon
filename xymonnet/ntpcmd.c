/* SPDX-License-Identifier: GPL-2.0-or-later                                  */
/*----------------------------------------------------------------------------*/
/* Xymon network test tool - NTP tool selection and command construction.     */
/*                                                                            */
/* Split out of run_ntp_service() so it can be driven directly by             */
/* tests/network/ntp-cmd-harness.c - the harness #includes this file the same */
/* way xymond/do_rrd.c #includes the do_*.c RRD parsers. The filesystem probe */
/* (which tools are installed) is injected as a callback so the selection     */
/* logic is testable without installing any NTP client.                       */
/*----------------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum ntptool_t { NTP_NONE = -1, NTP_NTPDATE, NTP_NTPDIG, NTP_SNTP, NTP_CHRONYD };

static const char *ntp_toolname[4] = { "ntpdate", "ntpdig", "sntp", "chronyd" };
static const char *ntp_toolenv[4]  = { "NTPDATE", "NTPDIG", "SNTP", "CHRONYD" };

/*
 * Pick the NTP query tool from NTPTOOL, a '|'-separated priority list of
 * "ntpdate", "ntpdig", "sntp" and "chronyd": the first installed tool wins, so
 * a single name forces that tool. avail(idx, ud) returns non-zero when the tool
 * with index idx is installed. ntptool is the NTPTOOL value (NULL when unset);
 * when it is NULL and sntp_set is true the legacy "SNTP" setting forces sntp.
 * Never returns NTP_NONE - if nothing in the list is installed it returns the
 * first valid choice anyway, so the missing-tool error shows up in the test
 * output; an empty/all-invalid list falls back to ntpdate.
 */
static enum ntptool_t ntp_select_tool(const char *ntptool, int sntp_set,
				      int (*avail)(int idx, void *ud), void *ud)
{
	const char *list = ntptool;
	char *copy, *tok, *sp;
	enum ntptool_t ntptool_sel = NTP_NONE, firstchoice = NTP_NONE;
	int i;

	if ((list == NULL) && sntp_set) list = "sntp";
	copy = strdup(list ? list : "ntpdig|ntpdate|sntp|chronyd");

	tok = strtok_r(copy, "|", &sp);
	while (tok && (ntptool_sel == NTP_NONE)) {
		for (i = 0; (i < 4) && (strcasecmp(tok, ntp_toolname[i]) != 0); i++) ;
		if (i == 4) {
			errprintf("Unknown NTPTOOL entry '%s' - ignored\n", tok);
		}
		else {
			if (firstchoice == NTP_NONE) firstchoice = i;
			if (avail(i, ud)) ntptool_sel = i;
		}
		tok = strtok_r(NULL, "|", &sp);
	}
	if (ntptool_sel == NTP_NONE) ntptool_sel = (firstchoice != NTP_NONE) ? firstchoice : NTP_NTPDATE;

	free(copy);
	return ntptool_sel;
}

/*
 * Build the shell command for the chosen tool into buf. opts is the tool's
 * *OPTS setting and ip the address under test; timeout is the per-host external
 * command timeout in seconds (extcmdtimeout-1 in production).
 *
 * The timeout flag is NOT shared across tools: ntpsec ntpdig takes '-t',
 * chronyd takes '-t' (exit after N seconds), but ntp.org sntp spells the
 * unicast-response timeout '-u' and has no '-t' at all. ntpsec renamed sntp to
 * ntpdig, so the NTP_SNTP branch only ever runs the classic ntp.org binary.
 */
static void ntp_build_cmd(char *buf, size_t bufsz, enum ntptool_t tool,
			  const char *cmdpath, const char *opts,
			  const char *ip, int timeout)
{
	switch (tool) {
	  case NTP_NTPDATE:
		snprintf(buf, bufsz, "%s %s %s 2>&1", cmdpath, opts, ip);
		break;
	  case NTP_NTPDIG:
		snprintf(buf, bufsz, "%s %s -t %d %s 2>&1", cmdpath, opts, timeout, ip);
		break;
	  case NTP_SNTP:
		snprintf(buf, bufsz, "%s %s -u %d %s 2>&1", cmdpath, opts, timeout, ip);
		break;
	  case NTP_CHRONYD:
		/* -Q = query only, never touches the clock; works unprivileged */
		snprintf(buf, bufsz, "%s -Q %s -t %d 'server %s iburst' 2>&1", cmdpath, opts, timeout, ip);
		break;
	  case NTP_NONE:
		/* Cannot happen - ntp_select_tool() always picks a tool */
		if (bufsz) buf[0] = '\0';
		break;
	}
}
