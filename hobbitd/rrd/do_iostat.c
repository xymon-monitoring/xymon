/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char iostat_rcsid[] = "$Id: do_iostat.c,v 1.15 2007-07-21 10:19:16 henrik Exp $";

int do_iostat_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
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

	static char *iostat_params[] = { "rrdcreate", rrdfn, 
					"DS:rs:GAUGE:600:1:U", "DS:ws:GAUGE:600:1:U", 
					"DS:krs:GAUGE:600:1:U", "DS:kws:GAUGE:600:1:U", 
					"DS:wait:GAUGE:600:1:U", "DS:actv:GAUGE:600:1:U", 
					"DS:wsvc_t:GAUGE:600:1:U", "DS:asvc_t:GAUGE:600:1:U", 
					"DS:w:GAUGE:600:1:U", "DS:b:GAUGE:600:1:U", 
					"DS:sw:GAUGE:600:1:U", "DS:hw:GAUGE:600:1:U", 
					"DS:trn:GAUGE:600:1:U", "DS:tot:GAUGE:600:1:U", 
					NULL };
	static char *iostat_tpl      = NULL;

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

	MEMDEFINE(marker);

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
						setupfn("iostat.%s.rrd", newkey->value);
						sprintf(rrdvalues, "%d:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f",
							(int) tstamp, 
							v[0], v[1], v[2], v[3], v[4], v[5], v[6],
							v[7], v[8], v[9], v[10], v[11], v[12], v[13]);
						create_and_update_rrd(hostname, testname, rrdfn, iostat_params, iostat_tpl);
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

	MEMUNDEFINE(marker);

	return 0;
}

