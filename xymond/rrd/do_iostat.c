/*----------------------------------------------------------------------------*/
/* Xymon RRD handler module.                                                  */
/*                                                                            */
/* Copyright (C) 2004-2011 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char iostat_rcsid[] = "$Id$";

static char *iostat_params[] = { "DS:rs:GAUGE:600:1:U", "DS:ws:GAUGE:600:1:U", 
				"DS:krs:GAUGE:600:1:U", "DS:kws:GAUGE:600:1:U", 
				"DS:wait:GAUGE:600:1:U", "DS:actv:GAUGE:600:1:U", 
				"DS:wsvc_t:GAUGE:600:1:U", "DS:asvc_t:GAUGE:600:1:U", 
				"DS:w:GAUGE:600:1:U", "DS:b:GAUGE:600:1:U", 
				"DS:sw:GAUGE:600:1:U", "DS:hw:GAUGE:600:1:U", 
				"DS:trn:GAUGE:600:1:U", "DS:tot:GAUGE:600:1:U", 
				NULL };
static void *iostat_tpl      = NULL;


int do_iostatdisk_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	char *dataline;

	/*
	 * This format is reported in the "iostatdisk" section:
	 *
	 * data HOSTNAME.iostatdisk
	 * solaris
	 * extended device statistics
	 * device,r/s,w/s,kr/s,kw/s,wait,actv,svc_t,%w,%b,
	 * dad0,a,0.0,0.7,0.0,5.8,0.0,0.0,4.3,0,0
	 * dad0,b,0.0,0.0,0.0,0.0,0.0,0.0,27.9,0,0
	 * dad0,c,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 * dad0,e,0.0,0.6,0.0,4.1,0.0,0.0,3.7,0,0
	 * dad0,f,0.0,17.2,0.0,89.7,0.0,0.0,0.2,0,0
	 * dad0,h,0.0,0.5,0.0,2.7,0.0,0.0,2.2,0,0
	 * dad1,c,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 * dad1,h,0.0,0.0,0.0,0.0,0.0,0.0,27.1,0,0
	 * nfs1,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 * extended device statistics
	 * device,r/s,w/s,kr/s,kw/s,wait,actv,svc_t,%w,%b,
	 * dad0,a,0.0,0.6,0.0,5.1,0.0,0.0,4.2,0,0
	 * dad0,b,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 * dad0,c,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 * dad0,e,0.0,0.5,0.0,3.4,0.0,0.0,3.2,0,0
	 * dad0,f,0.0,12.6,0.0,65.6,0.0,0.0,0.2,0,0
	 * dad0,h,0.0,0.4,0.0,2.4,0.0,0.0,1.8,0,0
	 * dad1,c,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 * dad1,h,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 * nfs1,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0,0
	 *
	 * There are two chunks of data: Like vmstat, we first get a
	 * summary at the start of data collection, and then another
	 * with the 5-minute average. So we must skip the first chunk.
	 *
	 * Note that real disks are identified by "dad0,a" whereas
	 * NFS mounts show up as "nfs1" (no comma!).
	 */

	if (iostat_tpl == NULL) iostat_tpl = setup_template(iostat_params);

	dataline = strstr(msg, "\ndevice,r/s,w/s,kr/s,kw/s,wait,actv,svc_t,%w,%b,");
	if (!dataline) return -1;
	dataline = strstr(dataline+1, "\ndevice,r/s,w/s,kr/s,kw/s,wait,actv,svc_t,%w,%b,");
	if (!dataline) return -1;

	dataline++;
	while (dataline && *dataline) {
		char *elems[12];
		char *eoln, *p, *id = "";
		int i, valofs;

		eoln = strchr(dataline, '\n'); if (eoln) *eoln = '\0';

		memset(elems, 0, sizeof(elems));
		p = elems[0] = dataline; i=0;
		do {
			p = strchr(p+1, ',');
			i++;
			if (p) {
				*p = '\0';
				elems[i] = p+1;
			}
		} while (p);

		if (elems[9] == NULL) goto nextline;
		else if (elems[10] == NULL) {
			/* NFS "disk" */
			id = elems[0];
			valofs = 1;
		}
		else {
			/* Normal disk - re-instate the "," between elems[0] and elems[1] */
			*(elems[1]-1) = ','; /* Hack! */
			valofs = 2;
		}

		setupfn2("%s.%s.rrd", "iostat", id);
		snprintf(rrdvalues, sizeof(rrdvalues), "%d:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s",
			(int) tstamp, 
			elems[valofs],		/* r/s */
			elems[valofs+1],	/* w/s */
			elems[valofs+2],	/* kr/s */
			elems[valofs+3],	/* kw/s */
			elems[valofs+4],	/* wait */
			elems[valofs+5],	/* actv */
			elems[valofs+6],	/* wsvc_t - we use svc_t here */
			"U",			/* asvc_t not in this format */
			elems[valofs+7],	/* %w */
			elems[valofs+8],	/* %b */
			"U", "U", "U", "U"	/* sw, hw, trn, tot not in this format */
		       );

nextline:
		dataline = (eoln ? eoln+1 : NULL);
	}

	return 0;
}

int do_iostat_rrd(char *hostname, char *testname, char *classname, char *pagepaths, char *msg, time_t tstamp)
{
	/*
	 * BEGINKEY
	 * d0 /
	 * d5 /var
	 * d6 /export
	 * ENDKEY
	 * BEGINDATA
	 *     r/s    w/s   kr/s   kw/s wait actv wsvc_t asvc_t  %w  %b s/w h/w trn tot device
	 *     0.9    2.8    7.3    1.8  0.0  0.0    2.7    9.3   1   2   0   0   0   0 d0
	 *     0.1    0.3    0.8    0.5  0.0  0.0    5.2   11.0   0   0   0   0   0   0 d5
	 *     0.1    0.2    1.0    1.1  0.0  0.0    6.9   12.9   0   0   0   0   0   0 d6
	 * ENDDATA
	 */

	typedef struct iostatkey_t { 
		char *key; 
		char *value; 
		struct iostatkey_t *next;
	} iostatkey_t;

	enum { S_NONE, S_KEYS, S_DATA } state;
	iostatkey_t *keyhead = NULL;
	iostatkey_t *newkey;
	char *eoln, *curline;
	char *buf, *p;
	float v[14];
	char marker[MAX_LINE_LEN];

	if (iostat_tpl == NULL) iostat_tpl = setup_template(iostat_params);

	curline = msg; state = S_NONE;
	while (curline) {
		eoln = strchr(curline, '\n'); if (eoln) *eoln = '\0';

		if (strncmp(curline, "BEGINKEY", 8) == 0) { state = S_KEYS; }
		else if (strncmp(curline, "ENDKEY", 6) == 0) { state = S_NONE; }
		else if (strncmp(curline, "BEGINDATA", 9) == 0) { state = S_DATA; }
		else if (strncmp(curline, "ENDDATA", 7) == 0) { state = S_NONE; }
		else {
			switch (state) {
			  case S_NONE:
				break;

			  case S_KEYS:
				buf = xstrdup(curline);
				newkey = (iostatkey_t *)xcalloc(1, sizeof(iostatkey_t));

				p = strtok(buf, " "); 
				if (p) newkey->key = xstrdup(p);

				p = strtok(NULL, " "); 
				if (p) {
					if (strcmp(p, "/") == 0) newkey->value = xstrdup(",root");
					else {
						newkey->value = xstrdup(p);
						p = newkey->value; while ((p = strchr(p, '/')) != NULL) *p = ',';
					}
				}
				xfree(buf);

				if (newkey->key && newkey->value) {
					newkey->next = keyhead; keyhead = newkey;
				}
				else {
					if (newkey->key) xfree(newkey->key);
					if (newkey->value) xfree(newkey->value);
					xfree(newkey);
				}
				break;

			  case S_DATA:
				buf = xstrdup(curline);
				if (sscanf(buf, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %s",
					   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
					   &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], marker) == 15) {

					/* Find the disk name */
					for (newkey = keyhead; (newkey && strcmp(newkey->key, marker)); newkey = newkey->next) ;

					if (newkey) {
						setupfn2("%s.%s.rrd", "iostat", newkey->value);
						snprintf(rrdvalues, sizeof(rrdvalues), "%d:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f",
							(int) tstamp, 
							v[0], v[1], v[2], v[3], v[4], v[5], v[6],
							v[7], v[8], v[9], v[10], v[11], v[12], v[13]);
						create_and_update_rrd(hostname, testname, classname, pagepaths, iostat_params, iostat_tpl);
					}
				}
				xfree(buf);
				break;
			}
		}

		if (eoln) { *eoln = '\n'; curline = eoln + 1; }
		else { curline = NULL; }
	}

	/* Free the keylist */
	while (keyhead) {
		newkey = keyhead;
		keyhead = keyhead->next;
		if (newkey->key) xfree(newkey->key);
		if (newkey->value) xfree(newkey->value);
		xfree(newkey);
	}

	return 0;
}

