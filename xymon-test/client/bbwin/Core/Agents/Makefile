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


DIRS = \
    common\~   		\
    externals\~   		\
    uptime\~   		\
    cpu\~   		\
    disk\~   		\
	filesystem\~   	\
    memory\~   		\
    msgs\~   		\
    stats\~   		\
    svcs\~   		\
    procs\~   		\
    sample\~   		\
	bbwinupdate\~   \
	who\~   \

!include <sdkpropbld.mak>
	
all: $(DIRS)	

$(DIRS):
!include <sdkbld.mak>


clean:
	cd common
	nmake clean
	cd ..\externals
	nmake clean
	cd ..\uptime
	nmake clean
	cd ..\cpu
	nmake clean
	cd ..\disk
	nmake clean
	cd ..\filesystem
	nmake clean
	cd ..\memory
	nmake clean
	cd ..\msgs
	nmake clean
	cd ..\stats
	nmake clean
	cd ..\procs
	nmake clean
	cd ..\sample
	nmake clean
	cd ..\bbwinupdate
	nmake clean
	cd ..\who
	nmake clean
	cd ..

	