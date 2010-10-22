/*----------------------------------------------------------------------------*/
/* Hobbit monitor library.                                                    */
/*                                                                            */
/* Copyright (C) 2002-2009 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __HOLIDAYS_H__
#define __HOLIDAYS_H__

typedef struct holiday_t {
	enum { HOL_ABSOLUTE, HOL_EASTER, HOL_ADVENT, 
	       HOL_MON_AFTER, HOL_TUE_AFTER, HOL_WED_AFTER, HOL_THU_AFTER, HOL_FRI_AFTER, HOL_SAT_AFTER, HOL_SUN_AFTER,
	       HOL_MON, HOL_TUE, HOL_WED, HOL_THU, HOL_FRI, HOL_SAT, HOL_SUN } holtype;

	char *desc;	/*	description					 	*/
	int month;  	/* 	month for absolute date				 	*/
	int day;    	/* 	day for absolute date or offset for type 2 and 3 	*/
	int yday;   	/* 	day of the year this holiday occurs in current year	*/
	int year;   	/* 	year for absolute date 					*/

	struct holiday_t *next;
} holiday_t;

extern int load_holidays(int year);
extern int getweekdayorholiday(char *key, struct tm *t);
extern char *isholiday(char *key, int dayinyear);
extern void printholidays(char *key, strbuffer_t *buf, int mfirst, int mlast);

#endif

