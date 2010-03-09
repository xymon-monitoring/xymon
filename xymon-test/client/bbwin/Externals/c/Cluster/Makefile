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

!IF "$(TARGETOS)" == "WINNT"

!include <Win32.Mak>

all: $(OUTDIR) $(OUTDIR)\Cluster.exe

SRC = 	Cluster.c \
	Version.c \
	Utils.c

OBJ	=$(OUTDIR)\Cluster.obj \
	 $(OUTDIR)\Version.obj \
	 $(OUTDIR)\Utils.obj

HEADERS =Cluster.h

cflags = $(cflags) -DUNICODE

$(OUTDIR) :
    if not exist "$(OUTDIR)/$(NULL)" mkdir $(OUTDIR)

$(OBJ): $(SRC) $(HEADERS)
	$(cc) $(cdebug) $(cflags) $(cvars) /Fo"$(OUTDIR)\\" /Fd"$(OUTDIR)\\" $(SRC)

$(OUTDIR)\Cluster.res: Cluster.rc Cluster.ico
	$(rc) $(rcflags) $(rcvars) /fo $(OUTDIR)\Cluster.res Cluster.rc

$(OUTDIR)\Cluster.exe: $(OUTDIR)\Cluster.obj $(OUTDIR)\Cluster.res
	$(link) $(ldebug) $(conlflags) -out:$(OUTDIR)\Cluster.exe $(OBJ) $(OUTDIR)\Cluster.res $(conlibs) clusapi.lib
	if not exist ..\..\..\Release mkdir ..\..\..\Release
	copy /Y $(OUTDIR)\Cluster.exe ..\..\..\Release

clean:
        $(CLEANUP)

!ELSE
!MESSAGE Sorry this sample only builds for the Windows NT Platform
!ENDIF
