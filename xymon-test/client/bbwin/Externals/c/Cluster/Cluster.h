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

   Cluster.h

    Header for Cluster utility

*/

#ifndef __CLUSTER_H__
#define __CLUSTER_H__


/*
** Defines
*/ 

// Somes usefull defines
#define BUFFER_SIZE 		2048

#define MAX_CLUSTER_GROUP	128

// Debug Mode 0 : off 1 : on
#define MODE_DEBUG			0
#define	PDEBUG				debug


#define USE_GREEN 		0
#define USE_YELLOW 		1
#define USE_RED			2

#define I_FALSE			0
#define I_TRUE			1

#define SAVE_NODE_FILENAME					L"BBClusterNodeName_"
#define SAVE_NODE_FILENAME_EXTENSION		L".txt"
#define	FILENAME_REPORT						L"cluster"

#define CLUSTER_NODE_HAS_CHANGED_FILE		L"NodeChanged_"

#define EXTERNALS_REG	L"SOFTWARE\\BBWin"

#define ERROR_GETTING_TEMP_PATH		L"Fatal Error can't get temporary folder path. please check %%temp%% or %%tmp%% system environement variables."

/*
** Structures
*/ 

typedef struct		cluster_group_s
{
	DWORD			state;
	DWORD			bbState;
	TCHAR 			name[BUFFER_SIZE];
	TCHAR 			nodeName[BUFFER_SIZE];
	DWORD			hasSavedNode;
	TCHAR			savedNodeName[BUFFER_SIZE];
	DWORD			nodeHasChanged;
	TCHAR			nodeChangedFilePath[BUFFER_SIZE];
	TCHAR			oldNodeName[BUFFER_SIZE];
}					cluster_group_t;

typedef struct 		cluster_s
{
	DWORD			finalState;
	LPWSTR			buffer;
	DWORD			countGroup;
	cluster_group_t	*groups[MAX_CLUSTER_GROUP];
}					cluster_t;

/*
** Prototypes
*/ 
void				debug(LPCWSTR format, ...);
void 				fatalError(LPCWSTR format, ...);
void 				warnError(LPCWSTR format, ...);

int					getTempPath(LPTSTR lpPath);

#endif // !__CLUSTER_H__
