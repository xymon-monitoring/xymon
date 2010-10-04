/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* This is a library module, part of libbbgen.                                */
/* It contains routines for time-specs in CRON format.                        */
/*                                                                            */
/* Copyright (C) 2010 Henrik Storner <henrik@storner.dk>                      */
/* Copyright (C) 2010 Milan Kocian <milon@wq.cz>                              */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2 (see the file "COPYING" for details), except as listed below.    */
/*                                                                            */
/*----------------------------------------------------------------------------*/

/*
 * A large part of this file was adapted from the "cron" sources by Paul
 * Vixie. It contains this copyright notice:
 * ------------------------------------------------------------------------
 * Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 * ------------------------------------------------------------------------
 * Adjusted by Milan Kocian <milon@wq.cz> so don't tease original autor
 * for my bugs.
 *
 * Major change is that functions operate on string instead on file.
 * And it's used only time part of cronline (no user, cmd,  etc.)


 * Also, the file "bitstring.h" was used from the cron sources. This file
 * carries the following copyright notice:
 * ------------------------------------------------------------------------
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Vixie.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)bitstring.h	5.2 (Berkeley) 4/4/90
 * ------------------------------------------------------------------------
 *
 */

static char rcsid[] = "$Id$";


/* --------------------  bitstring.h begins -----------------------------*/
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

typedef	unsigned char bitstr_t;

/* internal macros */
				/* byte of the bitstring bit is in */
#define	_bit_byte(bit) \
	((bit) >> 3)

				/* mask for the bit within its byte */
#define	_bit_mask(bit) \
	(1 << ((bit)&0x7))

/* external macros */
				/* bytes in a bitstring of nbits bits */
#define	bitstr_size(nbits) \
	((((nbits) - 1) >> 3) + 1)

				/* allocate a bitstring */
#define	bit_alloc(nbits) \
	(bitstr_t *)malloc(1, \
	    (unsigned int)bitstr_size(nbits) * sizeof(bitstr_t))

				/* allocate a bitstring on the stack */
#define	bit_decl(name, nbits) \
	(name)[bitstr_size(nbits)]

				/* is bit N of bitstring name set? */
#define	bit_test(name, bit) \
	((name)[_bit_byte(bit)] & _bit_mask(bit))

				/* set bit N of bitstring name */
#define	bit_set(name, bit) \
	(name)[_bit_byte(bit)] |= _bit_mask(bit)

				/* clear bit N of bitstring name */
#define	bit_clear(name, bit) \
	(name)[_bit_byte(bit)] &= ~_bit_mask(bit)

				/* clear bits start ... stop in bitstring */
#define	bit_nclear(name, start, stop) { \
	register bitstr_t *_name = name; \
	register int _start = start, _stop = stop; \
	register int _startbyte = _bit_byte(_start); \
	register int _stopbyte = _bit_byte(_stop); \
	if (_startbyte == _stopbyte) { \
		_name[_startbyte] &= ((0xff >> (8 - (_start&0x7))) | \
				      (0xff << ((_stop&0x7) + 1))); \
	} else { \
		_name[_startbyte] &= 0xff >> (8 - (_start&0x7)); \
		while (++_startbyte < _stopbyte) \
			_name[_startbyte] = 0; \
		_name[_stopbyte] &= 0xff << ((_stop&0x7) + 1); \
	} \
}

				/* set bits start ... stop in bitstring */
#define	bit_nset(name, start, stop) { \
	register bitstr_t *_name = name; \
	register int _start = start, _stop = stop; \
	register int _startbyte = _bit_byte(_start); \
	register int _stopbyte = _bit_byte(_stop); \
	if (_startbyte == _stopbyte) { \
		_name[_startbyte] |= ((0xff << (_start&0x7)) & \
				    (0xff >> (7 - (_stop&0x7)))); \
	} else { \
		_name[_startbyte] |= 0xff << ((_start)&0x7); \
		while (++_startbyte < _stopbyte) \
	    		_name[_startbyte] = 0xff; \
		_name[_stopbyte] |= 0xff >> (7 - (_stop&0x7)); \
	} \
}

				/* find first bit clear in name */
#define	bit_ffc(name, nbits, value) { \
	register bitstr_t *_name = name; \
	register int _byte, _nbits = nbits; \
	register int _stopbyte = _bit_byte(_nbits), _value = -1; \
	for (_byte = 0; _byte <= _stopbyte; ++_byte) \
		if (_name[_byte] != 0xff) { \
			_value = _byte << 3; \
			for (_stopbyte = _name[_byte]; (_stopbyte&0x1); \
			    ++_value, _stopbyte >>= 1); \
			break; \
		} \
	*(value) = _value; \
}

				/* find first bit set in name */
#define	bit_ffs(name, nbits, value) { \
	register bitstr_t *_name = name; \
	register int _byte, _nbits = nbits; \
	register int _stopbyte = _bit_byte(_nbits), _value = -1; \
	for (_byte = 0; _byte <= _stopbyte; ++_byte) \
		if (_name[_byte]) { \
			_value = _byte << 3; \
			for (_stopbyte = _name[_byte]; !(_stopbyte&0x1); \
			    ++_value, _stopbyte >>= 1); \
			break; \
		} \
	*(value) = _value; \
}

/* --------------------  end of bitstring.h ------------------------------- */

/* ---------------------   crondate from Paul Vixie cron ------------------ */

#define TRUE            1
#define FALSE           0
#define MAX_TEMPSTR     1000

#define Skip_Blanks(c) \
		while (*c == '\t' || *c == ' ') \
			c++;

#define Skip_Nonblanks(c) \
		while (*c!='\t' && *c!=' ' && *c!='\n' && *c!='\0') \
			c++;

#define SECONDS_PER_MINUTE 60

#define	FIRST_MINUTE	0
#define	LAST_MINUTE	59
#define	MINUTE_COUNT	(LAST_MINUTE - FIRST_MINUTE + 1)

#define	FIRST_HOUR	0
#define	LAST_HOUR	23
#define	HOUR_COUNT	(LAST_HOUR - FIRST_HOUR + 1)

#define	FIRST_DOM	1
#define	LAST_DOM	31
#define	DOM_COUNT	(LAST_DOM - FIRST_DOM + 1)

#define	FIRST_MONTH	1
#define	LAST_MONTH	12
#define	MONTH_COUNT	(LAST_MONTH - FIRST_MONTH + 1)

/* note on DOW: 0 and 7 are both Sunday, for compatibility reasons. */
#define	FIRST_DOW	0
#define	LAST_DOW	7
#define	DOW_COUNT	(LAST_DOW - FIRST_DOW + 1)

typedef	struct {
	bitstr_t	bit_decl(minute, MINUTE_COUNT);
	bitstr_t	bit_decl(hour,   HOUR_COUNT);
	bitstr_t	bit_decl(dom,    DOM_COUNT);
	bitstr_t	bit_decl(month,  MONTH_COUNT);
	bitstr_t	bit_decl(dow,    DOW_COUNT);
	int             flags;
#define DOM_STAR        0x01
#define DOW_STAR        0x02
#define WHEN_REBOOT     0x04
#define MIN_STAR        0x08
#define HR_STAR         0x10
} c_bits_t;

#define PPC_NULL        ((char **)NULL)

static
char    *MonthNames[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	NULL
};

static
char    *DowNames[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
	NULL
};

static char *
get_number(numptr, low, names, ch)
	int	*numptr;	/* where does the result go? */
	int	low;		/* offset applied to result if symbolic enum used */
	char	*names[];	/* symbolic names, if any, for enums */
	char	*ch;		/* current character */
{
	char	temp[MAX_TEMPSTR], *pc;
	int	len, i, all_digits;

	/* collect alphanumerics into our fixed-size temp array
	 */
	pc = temp;
	len = 0;
	all_digits = TRUE;
	while (isalnum((int)*ch)) {
		if (++len >= MAX_TEMPSTR)
			return(NULL);

		*pc++ = *ch;

		if (!isdigit((int)*ch))
			all_digits = FALSE;

		ch++;
	}
	*pc = '\0';

        if (len == 0) {
            return(NULL);
        }

	/* try to find the name in the name list
	 */
	if (names) {
		for (i = 0;  names[i] != NULL;  i++) {
			if (!strcasecmp(names[i], temp)) {
				*numptr = i+low;
				return ch;
			}
		}
	}

	/* no name list specified, or there is one and our string isn't
	 * in it.  either way: if it's all digits, use its magnitude.
	 * otherwise, it's an error.
	 */
	if (all_digits) {
		*numptr = atoi(temp);
		return ch;
	}

	return(NULL) ;
}

static int
set_element(bits, low, high, number)
	bitstr_t	*bits; 		/* one bit per flag, default=FALSE */
	int		low;
	int		high;
	int		number;
{
	if (number < low || number > high)
		return(-1);

	bit_set(bits, (number-low));
	return(0);
}


static char *
get_range(bits, low, high, names, ch, last)
	bitstr_t	*bits;		/* one bit per flag, default=FALSE */
	int		low, high;	/* bounds, impl. offset for bitstr */
	char		*names[];	/* NULL or names of elements */
	char		*ch;		/* current character being processed */
	int		last;		/* processing last value  */
{
	/* range = number | number "-" number [ "/" number ]
	 */

	register int	i;
	auto int	num1, num2, num3;

	if (*ch == '*') {
		/* '*' means "first-last" but can still be modified by /step
		 */
		num1 = low;
		num2 = high;
		ch++;
		if (!*ch) {
			if (!last)  /* string is too short (if not last)*/
				return(NULL);
		}
	} else {
		ch = get_number(&num1, low, names, ch);
		if (!ch)
			return (NULL);

		if (*ch != '-') {
			/* not a range, it's a single number.
			 */
			if (set_element(bits, low, high, num1))
				return(NULL);
			return ch;
		} else {
			/* eat the dash
			 */
			ch++;
			if (!*ch)
				return(NULL);
			/* get the number following the dash
			 */
			ch = get_number(&num2, low, names, ch);
			if (!ch)
				return(NULL);
		}
	}

	/* check for step size
	 */
	if (*ch == '/') {
		/* eat the slash
		 */
		ch++;
		if (!*ch)
			return(NULL);

		/* get the step size -- note: we don't pass the
		 * names here, because the number is not an
		 * element id, it's a step size.  'low' is
		 * sent as a 0 since there is no offset either.
		 */
		ch = get_number(&num3, 0, PPC_NULL, ch);
		if (!ch || num3 <= 0)
			return(NULL) ;
	} else {
		/* no step.  default==1.
		 */
		num3 = 1;
	}

	/* Explicitly check for sane values. Certain combinations of ranges and
	 * steps which should return EOF don't get picked up by the code below,
	 * eg:
	 *	5-64/30 * * * *	touch /dev/null
	 *
	 * Code adapted from set_elements() where this error was probably intended
	 * to be catched.
	 */
	if (num1 < low || num1 > high || num2 < low || num2 > high)
		return(NULL);

	/* range. set all elements from num1 to num2, stepping
	 * by num3.  (the step is a downward-compatible extension
	 * proposed conceptually by bob@acornrc, syntactically
	 * designed then implmented by paul vixie).
	 */
	for (i = num1;  i <= num2;  i += num3)
		if (set_element(bits, low, high, i))
			return(NULL);

	return ch;
}

static char *
get_list(bits, low, high, names, ch, last)
	bitstr_t	*bits;		/* one bit per flag, default=FALSE */
	int		low, high;	/* bounds, impl. offset for bitstr */
	char		*names[];	/* NULL or *[] of names for these elements */
	char		*ch;		/* current character being processed */
	int		last;		/* processing last value */
{
	register int	done;

	/* we know that we point to a non-blank character here;
	 * must do a Skip_Blanks before we exit, so that the
	 * next call (or the code that picks up the cmd) can
	 * assume the same thing.
	 */

	/* clear the bit string, since the default is 'off'.
	 */
	bit_nclear(bits, 0, (high-low+1));

	/* process all ranges
	 */
	done = FALSE;
	while (!done) {
		ch = get_range(bits, low, high, names, ch, last);
		if (ch && *ch == ',')
			ch++;
		else
			done = TRUE;
	}

	/* exiting.  skip to some blanks, then skip over the blanks.
	 */
	if (ch) {
		Skip_Nonblanks(ch)
		Skip_Blanks(ch)
	}

	return ch;
}


/* parse cron time */
void * parse_cron_time(char * ch) {
	c_bits_t *e;

	e = (c_bits_t *) calloc(1, sizeof(c_bits_t));
	if (!e)
		return(NULL);

	if (ch[0] == '@') {
		if (!strcmp("yearly", ch + 1) || !strcmp("annually", ch + 1)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_set(e->dom, 0);
			bit_set(e->month, 0);
			bit_nset(e->dow, 0, (LAST_DOW-FIRST_DOW+1));
			e->flags |= DOW_STAR;
		} else if (!strcmp("monthly", ch + 1)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_set(e->dom, 0);
			bit_nset(e->month, 0, (LAST_MONTH-FIRST_MONTH+1));
			bit_nset(e->dow, 0, (LAST_DOW-FIRST_DOW+1));
			e->flags |= DOW_STAR;
		} else if (!strcmp("weekly", ch + 1)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_nset(e->dom, 0, (LAST_DOM-FIRST_DOM+1));
			e->flags |= DOM_STAR;
			bit_nset(e->month, 0, (LAST_MONTH-FIRST_MONTH+1));
			bit_nset(e->dow, 0,0);
		} else if (!strcmp("daily", ch + 1) || !strcmp("midnight", ch + 1)) {
			bit_set(e->minute, 0);
			bit_set(e->hour, 0);
			bit_nset(e->dom, 0, (LAST_DOM-FIRST_DOM+1));
			bit_nset(e->month, 0, (LAST_MONTH-FIRST_MONTH+1));
			bit_nset(e->dow, 0, (LAST_DOW-FIRST_DOW+1));
		} else if (!strcmp("hourly", ch + 1)) {
			bit_set(e->minute, 0);
			bit_nset(e->hour, 0, (LAST_HOUR-FIRST_HOUR+1));
			bit_nset(e->dom, 0, (LAST_DOM-FIRST_DOM+1));
			bit_nset(e->month, 0, (LAST_MONTH-FIRST_MONTH+1));
			bit_nset(e->dow, 0, (LAST_DOW-FIRST_DOW+1));
			e->flags |= HR_STAR;
		} else {
			free(e);
			return(NULL);
		}
	} else {  /* end of '@' and begin for * * .. */
		if (*ch == '*')
			e->flags |= MIN_STAR;
		ch = get_list(e->minute, FIRST_MINUTE, LAST_MINUTE, PPC_NULL, ch, 0);
		if (!ch) {
			free(e);
			return(NULL);
		}
		/* hours
		 */
		if (*ch == '*')
			e->flags |= HR_STAR;
		ch = get_list(e->hour, FIRST_HOUR, LAST_HOUR, PPC_NULL, ch, 0);
		if (!ch) {
			free(e);
			return(NULL);
		}

		/* DOM (days of month)
		 */

		if (*ch == '*')
			e->flags |= DOM_STAR;
		ch = get_list(e->dom, FIRST_DOM, LAST_DOM, PPC_NULL, ch, 0);
		if (!ch) {
			free(e);
			return(NULL);
		}

		/* month
		 */

		ch = get_list(e->month, FIRST_MONTH, LAST_MONTH, MonthNames, ch, 0);
		if (!ch) {
			free(e);
			return(NULL);
		}

		/* DOW (days of week)
		 */

		if (*ch == '*')
			e->flags |= DOW_STAR;
		ch = get_list(e->dow, FIRST_DOW, LAST_DOW, DowNames, ch, 1);
		if (!ch) {
			free(e);
			return(NULL);
		}
	}

	/* make sundays equivilent */
	if (bit_test(e->dow, 0) || bit_test(e->dow, 7)) {
		bit_set(e->dow, 0);
		bit_set(e->dow, 7);
	}
	/* end of * * ... parse */
	return e;
} /* END of cron date-time parser */

/*----------------- End of code from Paul Vixie's cron sources ----------------*/

void crondatefree(void *vcdate)
{
	c_bits_t *cdate = (c_bits_t *)vcdate;

	free(cdate);
}

static int minute=-1, hour=-1, dom=-1, month=-1, dow=-1;

void crongettime(void)
{
	time_t now;
	struct tm tt;
	
	now = time(NULL); /* we need real clock, not monotonic from gettimer */
	localtime_r(&now, &tt);
	minute = tt.tm_min -FIRST_MINUTE;
	hour = tt.tm_hour -FIRST_HOUR;
	dom = tt.tm_mday -FIRST_DOM;
	month = tt.tm_mon +1 /* 0..11 -> 1..12 */ -FIRST_MONTH;
	dow = tt.tm_wday -FIRST_DOW;
}

int cronmatch(void *vcdate)
{
	c_bits_t *cdate = (c_bits_t *)vcdate;

	if (minute == -1) crongettime();

	return cdate
		&& bit_test(cdate->minute, minute)
		&& bit_test(cdate->hour, hour)
		&& bit_test(cdate->month, month)
		&& ( ((cdate->flags & DOM_STAR) || (cdate->flags & DOW_STAR))
		? (bit_test(cdate->dow,dow) && bit_test(cdate->dom,dom))
		: (bit_test(cdate->dow,dow) || bit_test(cdate->dom,dom)));
}

