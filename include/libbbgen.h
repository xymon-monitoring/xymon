/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

#ifndef __LIBBBGEN_H__
#define __LIBBBGEN_H__

#include <stdio.h>
#include <time.h>

typedef struct htnames_t {
	char *name;
	struct htnames_t *next;
} htnames_t;

#include "../lib/bblarrd.h"
#include "../lib/calc.h"
#include "../lib/color.h"
#include "../lib/digest.h"
#include "../lib/encoding.h"
#include "../lib/errormsg.h"
#include "../lib/headfoot.h"
#include "../lib/files.h"
#include "../lib/htmllog.h"
#include "../lib/misc.h"
#include "../lib/rbtr.h"
#include "../lib/sendmsg.h"
#include "../lib/sig.h"
#include "../lib/stackio.h"
#include "../lib/timefunc.h"
#include "../lib/timing.h"
#include "../lib/url.h"
#include "version.h"

#endif

