/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2007-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __READMIB_H__
#define __READMIB_H__


/* This holds one OID and our corresponding short-name */
typedef struct oidds_t {
	char *dsname;		/* Our short-name for the data item in the mib definition */
	char *oid;		/* The OID for the data in the mib definition */
	enum {
		RRD_NOTRACK,
		RRD_TRACK_GAUGE,
		RRD_TRACK_COUNTER,
		RRD_TRACK_DERIVE,
		RRD_TRACK_ABSOLUTE
	} rrdtype;		/* How to store this in an RRD file */

	enum {
		OID_CONVERT_NONE,	/* No conversion */
		OID_CONVERT_U32		/* Convert signed -> unsigned 32 bit integer */
	} conversion;
} oidds_t;

/* This holds a list of OID's and their shortnames */
typedef struct oidset_t {
	int oidsz, oidcount;
	oidds_t *oids;
	struct oidset_t *next;
} oidset_t;

/* This describes the ways we can index into this MIB */
enum mibidxtype_t { 
	MIB_INDEX_IN_OID, 	/*
				 * The index is part of the key-table OID's; by scanning the key-table
				 * values we find the one matching the wanted key, and can extract the
				 * index from the matching rows' OID.
				 * E.g. interfaces table looking for ifDescr or ifPhysAddress, or
				 * interfaces table looking for the extension-object ifName.
				 *   IF-MIB::ifDescr.1 = STRING: lo
				 *   IF-MIB::ifDescr.2 = STRING: eth1
				 *   IF-MIB::ifPhysAddress.1 = STRING:
				 *   IF-MIB::ifPhysAddress.2 = STRING: 0:e:a6:ce:cf:7f
				 *   IF-MIB::ifName.1 = STRING: lo
				 *   IF-MIB::ifName.2 = STRING: eth1
				 * The key table has an entry with the value = key-value. The index
				 * is then the part of the key-OID beyond the base key table OID.
				 */

	MIB_INDEX_IN_VALUE 	/*
				 * Index can be found by adding the key value as a part-OID to the
				 * base OID of the key (e.g. interfaces by IP-address).
				 *   IP-MIB::ipAdEntIfIndex.127.0.0.1 = INTEGER: 1
				 *   IP-MIB::ipAdEntIfIndex.172.16.10.100 = INTEGER: 3
				 */
};

typedef struct mibidx_t {
	char marker;				/* Marker character for key OID */
	enum mibidxtype_t idxtype;		/* How to interpret the key */
	char *keyoid;				/* Key OID */
	void *rootoid;				/* Binary representation of keyoid */
	unsigned int rootoidlen;		/* Length of binary keyoid */
	struct mibidx_t *next;
} mibidx_t;

typedef struct mibdef_t {
	enum { 
		MIB_STATUS_NOTLOADED, 		/* Not loaded */
		MIB_STATUS_LOADED, 		/* Loaded OK, can be used */
		MIB_STATUS_LOADFAILED 		/* Load failed (e.g. missing MIB file) */
	} loadstatus;
	char *mibfn;				/* Filename of the MIB file (for non-standard MIB's) */
	char *mibname;				/* MIB definition name */
	int tabular;				/* Does the MIB contain a table ? Or just simple data */
	oidset_t *oidlisthead, *oidlisttail;	/* The list of OID's in the MIB set */
	mibidx_t *idxlist;			/* List of the possible indices used for the MIB */
	int haveresult;				/* Used while building result messages */
	strbuffer_t *resultbuf;			/* Used while building result messages */
} mibdef_t;


extern int readmibs(char *cfgfn, int verbose);
extern mibdef_t *first_mib(void);
extern mibdef_t *next_mib(void);
extern mibdef_t *find_mib(char *mibname);

#endif

