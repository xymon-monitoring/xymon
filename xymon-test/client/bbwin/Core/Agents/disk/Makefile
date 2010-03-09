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

all: disk.dll

# BOOST_INC_PATH = /I C:\boost\include\boost-1_33_1
# BOOST_LIB_PATH = /LIBPATH:C:\boost\lib
# BOOST_LIB = $(BOOST_LIB_PATH) libboost_date_time-vc71-mt-s.lib

BOOST_INC_PATH = /I C:\boost\boost_1_36_0
BOOST_LIB_PATH = /LIBPATH:C:\boost\boost_1_36_0\lib
BOOST_LIB = $(BOOST_LIB_PATH) libboost_date_time-vc90-mt-s-1_36.lib


cflags = $(cflags) /EHsc /I ../.. $(BOOST_INC_PATH)

.cpp.obj:
    $(cc) $(cdebug) $(cflags) $(cvarsmt) $**		
	
#disk dependencies

disk_objs = 	disk.obj

disk.res: disk.rc
	$(rc) $(rcflags) $(rcvars) /fo disk.res disk.rc
	
disk.dll: $(disk_objs) disk.res
    $(link) /DLL $(dllldebug) $(disk_objs) disk.res -out:disk.dll $(BOOST_LIB)
	if not exist ..\..\..\Release mkdir ..\..\..\Release
	copy /Y disk.dll ..\..\..\Release

clean:
		erase /Q /S $(disk_objs)
