/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004-2006 Francesco Duranti <fduranti@q8.it>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

/*
  Some function used by do_netapp.pl do_beastat.pl and do_dbcheck.pl
*/

static char fd_lib_rcs[] = "$Id: do_fd_lib.c,v 1.00 2006/05/31 20:28:44 henrik Rel $";

unsigned long get_kb_data(char *msg,char *search)
{
	char *p,*r,*eoln;
	unsigned long value=0;

	/* go to the start of the message */
	p = strstr(msg, search);
	if (p) {
		/* set the endofline */
		eoln = strchr(p, '\n'); if (eoln) *eoln = '\0';

		/* search for a ":" or "=" */
		if ((r = strchr(p,':')) == NULL) r=strchr(p,'=');
//		dbgprintf("1getdata %s\n",p);
//		dbgprintf("2getdata %s\n",r);
		if (r) {
			r++;
			value=atol(r);
                        /* Convert to KB if there's a modifier after the numbers */
                        r += strspn(r, "0123456789. ");
                        if (*r == 'M') value *= 1024;
                        else if (*r == 'G') value *= (1024*1024);
                        else if (*r == 'T') value *= (1024*1024*1024);
		}
		/* reset the endofline */
		if (eoln) *eoln = '\n';
	} 
	return value;
}

unsigned long get_long_data(char *msg,char *search)
{
	char *p,*r,*eoln;
	unsigned long value=0;

	/* go to the start of the message */
	p = strstr(msg, search);
	if (p) {
		/* set the endofline */
		eoln = strchr(p, '\n'); if (eoln) *eoln = '\0';

		/* search for a ":" or "=" */
		if ((r = strchr(p, ':')) == NULL) r=strchr(p, '=');
//		dbgprintf("1getdata %s\n",p);
//		dbgprintf("2getdata %s\n",r);
		if (r) {
			value=atol(++r);
		}
		/* reset the endofline */
		if (eoln) *eoln = '\n';
	} 
	return value;
}

double get_double_data(char *msg,char *search)
{
	char *p,*r,*eoln;
	double value=0;

	/* go to the start of the message */
	p = strstr(msg, search);
	if (p) {
		/* set the endofline */
		eoln = strchr(p, '\n'); if (eoln) *eoln = '\0';

		/* search for a ":" or "=" */
		if ((r = strchr(p,':')) == NULL) r=strchr(p,'=');
//		dbgprintf("1getdata %s\n",p);
//		dbgprintf("2getdata %s\n",r);
		if (r) value=atof(++r);
		/* reset the endofline */
		if (eoln) *eoln = '\n';
	} 
	return value;
}

int get_int_data(char *msg,char *search)
{
	char *p,*r,*eoln;
	int value=0;

	/* go to the start of the message */
	p = strstr(msg, search);
	if (p) {
		/* set the endofline */
		eoln = strchr(p, '\n'); if (eoln) *eoln = '\0';
		/* search for a ":" or "=" */
		if ((r = strchr(p,':')) == NULL) r=strchr(p,'=');
//		dbgprintf("\n1getdata\n%s\n",p);
//		dbgprintf("\n2getdata\n%s\n",r);
		if (r)  value=atoi(++r); 
		/* reset the endofline */
		if (eoln) *eoln = '\n';
	} 
	return value;
}


typedef struct sectlist_t {
	char *sname;
	char *sdata;
	struct sectlist_t *next;
} sectlist_t;
sectlist_t *sections = NULL;

void splitmsg(char *clientdata)
{
	char *cursection, *nextsection;
	char *sectname, *sectdata;

	/* Free the old list */
	if (sections) {
		sectlist_t *swalk, *stmp;

		swalk = sections;
		while (swalk) {
			stmp = swalk;
			swalk = swalk->next;
			xfree(stmp);
		}

		sections = NULL;
	}

	if (clientdata == NULL) {
		errprintf("Got a NULL client data message\n");
		return;
	}

	/* Find the start of the first section */
	if (*clientdata == '[') 
		cursection = clientdata; 
	else {
		cursection = strstr(clientdata, "\n[");
		if (cursection) cursection++;
	}

	while (cursection) {
		sectlist_t *newsect = (sectlist_t *)malloc(sizeof(sectlist_t));

		/* Find end of this section (i.e. start of the next section, if any) */
		nextsection = strstr(cursection, "\n[");
		if (nextsection) {
			*nextsection = '\0';
			nextsection++;
		}

		/* Pick out the section name and data */
		sectname = cursection+1;
		sectdata = sectname + strcspn(sectname, "]\n");
		*sectdata = '\0'; sectdata++; if (*sectdata == '\n') sectdata++;

		/* Save the pointers in the list */
		newsect->sname = sectname;
		newsect->sdata = sectdata;
		newsect->next = sections;
		sections = newsect;

		/* Next section, please */
		cursection = nextsection;
	}
}

char *getdata(char *sectionname)
{
	sectlist_t *swalk;

	for (swalk = sections; (swalk && strcmp(swalk->sname, sectionname)); swalk = swalk->next) ;
	if (swalk) return swalk->sdata;

	return NULL;
}

