/*
this file is part of BBWin
Copyright (C)2006 Etienne GRIGNON  ( etienne.grignon@gmail.com )

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Utils.cpp

    Utils functions

*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "Cluster.h"

void				debug(LPCWSTR format, ...)
{
	static int 		n_debug = 1;
	va_list       	ap;
	
	if (MODE_DEBUG)
	{
		va_start(ap, format);
		fwprintf(stdout, L"DEBUG[%04i]: ", n_debug++);
		vfwprintf(stdout, format, ap);
		fwprintf(stdout, L"\n");
		va_end(ap);
	}
}

void 				fatalError(LPCWSTR format, ...)
{
	va_list       	ap;
	
	va_start(ap, format);
	vfwprintf(stdout, format, ap);
	fwprintf(stdout, L"\n");
	va_end(ap);
	exit(1);
}

void 				warnError(LPCWSTR format, ...)
{
	va_list       	ap;
	
	va_start(ap, format);
	vfwprintf(stdout, format, ap);
	fwprintf(stdout, L"\n");
	va_end(ap);
}

