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

all: cpu.dll

# BOOST_INC_PATH = /I C:\boost\include\boost-1_33_1
# BOOST_LIB_PATH = /LIBPATH:C:\boost\lib
# BOOST_LIB = $(BOOST_LIB_PATH) libboost_date_time-vc71-mt-s.lib

BOOST_INC_PATH = /I C:\boost\boost_1_36_0
BOOST_LIB_PATH = /LIBPATH:C:\boost\boost_1_36_0\lib
BOOST_LIB = $(BOOST_LIB_PATH) libboost_date_time-vc90-mt-s-1_36.lib

cflags = $(cflags) /EHsc /I ../.. /I ../common $(BOOST_INC_PATH)

.cpp.obj:
#    $(cc) $(cdebug) $(cflags) $(cvarsmt) $**		
	$(cc) $(cdebug) $(cflags) $**		

## cpu dependencies

cpu_objs = 	cpu.obj CpuUsage.obj 
common_objs = ../common/SystemCounters.obj
cpu.res: cpu.rc
	$(rc) $(rcflags) $(rcvars) /fo cpu.res cpu.rc

cpu.dll: $(cpu_objs) cpu.res $(common_objs)
	$(link) /NODEFAULTLIB:library /DLL $(dllldebug) $(cpu_objs) $(common_objs) cpu.res -out:cpu.dll $(BOOST_LIB)
	if not exist ..\..\..\Release mkdir ..\..\..\Release
	copy /Y cpu.dll ..\..\..\Release

clean:
	erase /Q /S $(cpu_objs)




