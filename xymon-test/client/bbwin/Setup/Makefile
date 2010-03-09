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

PROJ = BBWin

all: $(PROJ).msi

# Define project specific macros
PROJ_OBJS  = UpgradeConfiguration.obj
BASE_OBJS  =
EXTRA_LIBS = msi.lib
GLOBAL_DEP =
RC_DEP     =
CL_PRE 	   =

.SUFFIXES: .wxs .wixobj

WIXCC = candle
WCFLAG = /nologo
WIXLINKER = light
WLFLAG = /nologo

###########################
# makefile inference rules
###########################
 
.wxs.wixobj:
	$(WIXCC) $(WCFLAG) $(PROJ).wxs
 
.wixobj.msi:
	$(WIXLINKER) $(WLFLAG) $(PROJ).wixobj


# Inference rule for updating the object files
.cpp.obj:
    $(cc) $(CL_PRE) $(cdebug) $(cflags) $(cvarsdll) $**

# Build rule for resource file
$(PROJ).res: $(PROJ).rc $(RC_DEP)
   $(rc) $(rcflags) $(rcvars) /fo $(PROJ).res $(PROJ).rc

# Build rule for DLL
$(PROJ).dll: $(BASE_OBJS) $(PROJ_OBJS) $(PROJ).res
	$(link) $(ldebug) $(dlllflags) $(guilibsdll) \
                $(BASE_OBJS) $(PROJ_OBJS) $(EXTRA_LIBS) $(PROJ).res \
                -out:$(PROJ).dll $(MAPFILE)

$(PROJ).msi: $(PROJ).wixobj


# Rules for cleaning out those old files
clean:
	erase /q /s $(PROJ).dll $(PROJ).exp $(PROJ_OBJS) $(PROJ).res $(PROJ).wixobj $(PROJ).msi $(PROJ).lib

