/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2005-2008 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char snmpmib_rcsid[] = "$Id: do_snmpmib.c,v 1.1 2008-01-04 21:26:55 henrik Exp $";

static char *snmpmib_params[] = { 
				"DS:InPkts:COUNTER:600:0:U",
				"DS:OutPkts:COUNTER:600:0:U",
				"DS:InBadVersions:COUNTER:600:0:U",
				"DS:InBadCommunityName:COUNTER:600:0:U",
				"DS:InBadcommunityUses:COUNTER:600:0:U",
				"DS:InASMParseErrs:COUNTER:600:0:U",
				"DS:InTooBigs:COUNTER:600:0:U",
				"DS:InNoSuchNames:COUNTER:600:0:U",
				"DS:InBadValues:COUNTER:600:0:U",
				"DS:InReadOnlys:COUNTER:600:0:U",
				"DS:InGenErrs:COUNTER:600:0:U",
				"DS:InTotalReqVars:COUNTER:600:0:U",
				"DS:InTotalSetVars:COUNTER:600:0:U",
				"DS:InGetRequests:COUNTER:600:0:U",
				"DS:InGetNexts:COUNTER:600:0:U",
				"DS:InSetRequests:COUNTER:600:0:U",
				"DS:InGetResponses:COUNTER:600:0:U",
				"DS:InTraps:COUNTER:600:0:U",
				"DS:OutTooBigs:COUNTER:600:0:U",
				"DS:OutNoSuchNames:COUNTER:600:0:U",
				"DS:OutBadValues:COUNTER:600:0:U",
				"DS:OutGenErrs:COUNTER:600:0:U",
				"DS:OutGetRequests:COUNTER:600:0:U",
				"DS:OutGetNexts:COUNTER:600:0:U",
				"DS:OutSetRequests:COUNTER:600:0:U",
				"DS:OutGetResponses:COUNTER:600:0:U",
				"DS:OutTraps:COUNTER:600:0:U",
				"DS:SilentDrops:COUNTER:600:0:U",
				"DS:ProxyDrops:COUNTER:600:0:U",
			 	NULL };
static char *snmpmib_tpl      = NULL;

static char *snmpmib_valnames[] = {
	"snmpInPkts",
	"snmpOutPkts",
	"snmpInBadVersions",
	"snmpInBadCommunityNames",
	"snmpInBadcommunityUses",
	"snmpInASMParseErrs",
	"snmpInTooBigs",
	"snmpInNoSuchNames",
	"snmpInBadValues",
	"snmpInReadOnlys",
	"snmpInGenErrs",
	"snmpInTotalReqVars",
	"snmpInTotalSetVars",
	"snmpInGetRequests",
	"snmpInGetNexts",
	"snmpInSetRequests",
	"snmpInGetResponses",
	"snmpInTraps",
	"snmpOutTooBigs",
	"snmpOutNoSuchNames",
	"snmpOutBadValues",
	"snmpOutGenErrs",
	"snmpOutGetRequests",
	"snmpOutGetNexts",
	"snmpOutSetRequests",
	"snmpOutGetResponses",
	"snmpOutTraps",
	"snmpSilentDrops",
	"snmpProxyDrops",
	NULL
};

int do_snmpmib_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	return do_simple_mib_rrd(hostname, testname, msg, tstamp,
				 (sizeof(snmpmib_valnames) / sizeof(snmpmib_valnames[0])),
				 snmpmib_valnames, snmpmib_params, &snmpmib_tpl);
}

