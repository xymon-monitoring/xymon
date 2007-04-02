/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2006 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HOLIDAYS_H__
#define __HOLIDAYS_H__

typedef struct holiday_t {
	enum { HOL_ABSOLUTE, HOL_EASTER, HOL_ADVENT } holtype;

	char *desc;	/*	description					 	*/
	int month;  	/* 	month for absolute date				 	*/
	int day;    	/* 	day for absolute date or offset for type 2 and 3 	*/
	int yday;   	/* 	day of the year this holiday occurs in current year	*/

	struct holiday_t *next;
} holiday_t;

extern int load_holidays(void);
extern int getweekdayorholiday(char *key, struct tm *t);
extern char *isholiday(char *key, int dayinyear, int year);
extern void printholidays(char *key, int year, strbuffer_t *buf);

#endif

