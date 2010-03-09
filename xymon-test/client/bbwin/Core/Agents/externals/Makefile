#this file is part of BBWin
#Copyright (C)2006 Etienne GRIGNON  ( etienne.grignon@gmail.com )
#
#This program is free software; you can redistribute it and/or
#modify it under the terms of the GNU General Public License
#as published by the Free Software Foundation; either
#version 2 of the License, or (at your option) any later version.
#
#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#GNU General Public License for more details.
#
#You should have received a copy of the GNU General Public License
#along with this program; if not, write to the Free Software
#Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

!include <Win32.Mak>

all: externals.dll

cflags = $(cflags) /EHsc /I ../..

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $**		
	
## externals dependencies

externals_objs = 	externals.obj external.obj ou_thread.obj

externals.res: externals.rc
	$(rc) $(rcflags) $(rcvars) /fo externals.res externals.rc
	
externals.dll: $(externals_objs) externals.res
    $(link) /DLL $(dllldebug) $(externals_objs) externals.res -out:externals.dll
	if not exist ..\..\..\Release mkdir ..\..\..\Release
	copy /Y externals.dll ..\..\..\Release

clean:
		erase /Q /S $(externals_objs)
