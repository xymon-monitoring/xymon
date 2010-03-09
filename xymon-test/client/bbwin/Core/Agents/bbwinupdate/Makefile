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

all: bbwinupdate.dll

cflags = $(cflags) /EHsc /I ../.. /I ../../digest

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $**		
	
## bbwinupdate dependencies

bbwinupdate_objs = 	bbwinupdate.obj

bbwinupdate.res: bbwinupdate.rc
	$(rc) $(rcflags) $(rcvars) /fo bbwinupdate.res bbwinupdate.rc
	
bbwinupdate.dll: $(bbwinupdate_objs) bbwinupdate.res
    $(link) /DLL $(dllldebug) $(bbwinupdate_objs) bbwinupdate.res ../../../Release/digest.lib ../../../Release/TinyXml.lib kernel32.lib -out:bbwinupdate.dll
	if not exist ..\..\..\Release mkdir ..\..\..\Release
	copy /Y bbwinupdate.dll ..\..\..\Release
	
clean:
		erase /Q /S $(bbwinupdate_objs)


