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

    Cluster.c

    Get Cluster status

*/

#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <winbase.h>
#include <stdlib.h>

#include <time.h>
#include <winsock.h>

#include <clusapi.h>
#include <memory.h>

#include "cluster.h"

const TCHAR 	*bbcolors[] = { TEXT("green"), TEXT("yellow"), TEXT("red"), NULL};

int			getExternalPath(LPWSTR location)
{
	HKEY 	hKey;
	DWORD 	dwBuflen;
	LONG 	lRet;

    lRet = RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
    EXTERNALS_REG,
    0, KEY_QUERY_VALUE, &hKey);
    if (lRet != ERROR_SUCCESS) 
	{
		return 1;
    }
    if (lRet == ERROR_SUCCESS) 
	{
        dwBuflen = BUFFER_SIZE;
        lRet = RegQueryValueExW(hKey, TEXT("tmpPath"), NULL, NULL, (LPBYTE)location, &dwBuflen);
		if (lRet != ERROR_SUCCESS)
		{
			return 1;
		}
    }
    RegCloseKey(hKey);
	return 0;
}

int								loadNodeName(LPWSTR nodeName, LPWSTR groupName)
{
	FILE						*fp;
	static TCHAR				lpPath[BUFFER_SIZE];
	
	if (getTempPath((LPWSTR)lpPath) == 0)
		fatalError(ERROR_GETTING_TEMP_PATH);		
	wcscat((LPWSTR)lpPath, SAVE_NODE_FILENAME);
	wcscat((LPWSTR)lpPath, groupName);
	wcscat((LPWSTR)lpPath, SAVE_NODE_FILENAME_EXTENSION);
	if ((fp = _wfopen(lpPath, L"r")) == NULL)
	{
		warnError(L"Warning : can't open %s.", lpPath);
		return 0;
	}
	fwscanf(fp, L"%512s", nodeName );
	fclose(fp);
	return 1;
}

int								saveNodeName(LPWSTR lpszNodeName, LPWSTR groupName)
{
	FILE						*fp;
	static TCHAR				lpPath[BUFFER_SIZE];
	
	if (getTempPath((LPWSTR)lpPath) == 0)
		fatalError(ERROR_GETTING_TEMP_PATH);		
	wcscat((LPWSTR)lpPath, SAVE_NODE_FILENAME);
	wcscat((LPWSTR)lpPath, groupName);
	wcscat((LPWSTR)lpPath, SAVE_NODE_FILENAME_EXTENSION);
	if ((fp = _wfopen(lpPath, L"w")) == NULL)
	{
		warnError(L"Warning : can't open %s.", lpPath);
		return 1;
	}
	fwprintf(fp, L"%s", lpszNodeName);
	fclose(fp);
	return 0;
}

int							isNodeChangedFileExists(cluster_group_t *pGroupClust)
{
	FILE					*fp;
	
	if ((fp = _wfopen(pGroupClust->nodeChangedFilePath, L"r")) == NULL)
		return I_FALSE;
	fwscanf(fp, L"%512s", pGroupClust->oldNodeName);
	fclose(fp);
	return I_TRUE;
}

int								createNodeChangedFile(cluster_group_t *pGroupClust)
{
	FILE						*fp;
	
	if ((fp = _wfopen(pGroupClust->nodeChangedFilePath, L"w")) == NULL)
	{
		warnError(L"Warning : can't open %s.", pGroupClust->nodeChangedFilePath);
		return 1;
	}
	fwprintf(fp, L"%s", pGroupClust->savedNodeName);
	fclose(fp);
	return 0;
}

cluster_group_t				*initClusterGroup(LPWSTR groupName, CLUSTER_GROUP_STATE groupState)
{
	cluster_group_t			*pGroupClust;
	
	pGroupClust = LocalAlloc(LPTR, sizeof(cluster_group_t));
	wcsncpy(pGroupClust->name, groupName, BUFFER_SIZE - 1);
	pGroupClust->state = groupState;
	pGroupClust->bbState = USE_GREEN;
	pGroupClust->hasSavedNode = I_FALSE;
	switch (groupState)
	{
		case ClusterGroupFailed :
		case ClusterGroupPending :
		case ClusterGroupPartialOnline :
		case ClusterGroupOffline :
			pGroupClust->bbState = USE_YELLOW;
			break ;
		default :
			break ;
	}
	pGroupClust->nodeHasChanged = I_FALSE;
	if (getTempPath((LPWSTR)pGroupClust->nodeChangedFilePath) == 0)
		fatalError(ERROR_GETTING_TEMP_PATH);
	wcscat((LPWSTR)pGroupClust->nodeChangedFilePath, CLUSTER_NODE_HAS_CHANGED_FILE);
	wcscat((LPWSTR)pGroupClust->nodeChangedFilePath, groupName);
	return pGroupClust;
}

int						getGroupState(cluster_t *pcluster, HCLUSTER hClust, LPWSTR groupName)
{
	HGROUP 				hGroup;
	CLUSTER_GROUP_STATE	groupState;
	TCHAR				lpszNodeName[BUFFER_SIZE];
	DWORD				cchNodeName;
	cluster_group_t		*group;
	
	if ((hGroup = OpenClusterGroup(hClust, groupName)) == NULL)
	{
		warnError(L"Error 0x%x executing OpenCluster entry point.", GetLastError());
		return I_FALSE;
	}
	cchNodeName = BUFFER_SIZE;
	if ((groupState = GetClusterGroupState(hGroup, lpszNodeName, &cchNodeName)) == ClusterGroupStateUnknown)
	{
		warnError(L"Error 0x%x executing OpenCluster entry point.", GetLastError());
		LocalFree(lpszNodeName);
		CloseClusterGroup(hGroup);
		return I_FALSE;
	}
	PDEBUG(L"Getting Group \"%s\"", groupName);
	group = initClusterGroup(groupName, groupState);
	wcsncpy(group->nodeName, lpszNodeName, BUFFER_SIZE - 1);
	if (loadNodeName(group->savedNodeName, groupName))
		group->hasSavedNode = I_TRUE;
	if (group->hasSavedNode == I_TRUE && wcscmp(group->nodeName, group->savedNodeName) != 0)
	{
		group->bbState = USE_YELLOW;
		createNodeChangedFile(group);
		group->nodeHasChanged = I_TRUE;
	}
	if (isNodeChangedFileExists(group))
	{
		group->nodeHasChanged = I_TRUE;
		group->bbState = USE_YELLOW;
	}
	if (group->bbState == USE_YELLOW)
		pcluster->finalState = USE_YELLOW;
	saveNodeName(lpszNodeName, groupName);
	CloseClusterGroup(hGroup);
	pcluster->groups[pcluster->countGroup] = group;
	pcluster->countGroup++;
	return I_TRUE;
}

int					getClusterGroups(cluster_t *pcluster)
{
	HCLUSTER 		hClust;
	HCLUSENUM 		hClusEnum;
	DWORD			dwIndex;
	DWORD			dwType;
	LPWSTR 			lpszName;			
	DWORD			cchName;
	
	hClust = OpenCluster(NULL);
	if (hClust == NULL)
	{
		warnError(L"Error 0x%x executing OpenCluster entry point.", GetLastError());
        return I_FALSE;
	}
	if ((hClusEnum = ClusterOpenEnum(hClust, CLUSTER_ENUM_GROUP)) == NULL)
	{
		warnError(L"Error 0x%x executing OpenCluster entry point.", GetLastError());
		CloseCluster(hClust);
        return I_FALSE;
	}
	if ((lpszName = LocalAlloc(LPTR, BUFFER_SIZE * sizeof(TCHAR))) == NULL)
		fatalError(L"Fatal Error 0x%x executing LocalAlloc entry point.", GetLastError());
	cchName = BUFFER_SIZE;
	for (dwIndex = 0; ClusterEnum(hClusEnum, dwIndex, &dwType, lpszName, &cchName) == ERROR_SUCCESS; dwIndex++)
	{
		getGroupState(pcluster, hClust, lpszName);
		cchName = BUFFER_SIZE;
	}
	LocalFree(lpszName);
	ClusterCloseEnum(hClusEnum);
	CloseCluster(hClust);
	return I_TRUE;
}

void				initCluster(cluster_t *pclust)
{
	DWORD			inc;
	
	pclust->finalState = USE_GREEN;
	pclust->countGroup = 0;
	for (inc = 0; inc < MAX_CLUSTER_GROUP; inc++)
	{
		pclust->groups[inc] = NULL;
	}

}

void				freeCluster(cluster_t *pclust)
{
	DWORD			inc;
	
	for (inc = 0; inc < pclust->countGroup; inc++)
		LocalFree(pclust->groups[inc]);
}

//
// Check cluster state status
//
int					checkClusterState()
{
	HCLUSTER 		hClust;
	DWORD			pdwClusterState;
	
	if (GetNodeClusterState(NULL, &pdwClusterState) != ERROR_SUCCESS)
	{
		warnError(L"Error 0x%x executing GetClusterInformation entry point.", GetLastError());
        return I_FALSE;
	}
	switch (pdwClusterState)
	{
		case ClusterStateNotInstalled :
			warnError(L"Windows Clustering is not installed.");
			return I_FALSE;
		case ClusterStateNotConfigured :
			warnError(L"Windows Clustering is not configured.");
			return I_FALSE;
		case ClusterStateNotRunning :
		case ClusterStateRunning :
			break ;
	}	
	hClust = OpenCluster(NULL);
	if (hClust == NULL)
	{
		warnError(L"Error 0x%x executing OpenCluster entry point.", GetLastError());
        return I_FALSE;
	}
	CloseCluster(hClust);
	return I_TRUE;
}

int							generateReport(cluster_t *pclust)
{
	FILE					*fp;
	static TCHAR			lpPath[BUFFER_SIZE * sizeof(TCHAR)];
	DWORD					inc;
	DWORD					hasNodesChanged;
	
	hasNodesChanged = I_FALSE;
	if (getExternalPath((LPWSTR)lpPath) != 0)
		fatalError(L"Fatal Error can't get externals folder path. Please check registry %s", EXTERNALS_REG);
	wcscat((LPWSTR)lpPath, L"\\");
	wcscat((LPWSTR)lpPath, FILENAME_REPORT);
	wprintf(L"%s\n", lpPath);
	if ((fp = _wfopen(lpPath, L"w")) == NULL)
	{
		fatalError(L"Warning : can't open %s.", lpPath);
		return 1;
	}
	fwprintf(fp, L"%s\n\n\n", bbcolors[pclust->finalState]);
	fwprintf(fp, L"<b>--- Windows Cluster State ---</b>\n\n");
	fwprintf(fp, L"<table border=1>\n");
	fwprintf(fp, L"<tr><td>Group Name</td><td>Current Node</td><td>State</td><td>Status</td></tr>\n");
	for (inc = 0; inc < pclust->countGroup; ++inc)
	{
		fwprintf(fp, L"<tr><td>%s</td>", pclust->groups[inc]->name);
		fwprintf(fp, L"<td>%s", pclust->groups[inc]->nodeName);
		if (pclust->groups[inc]->nodeHasChanged == I_TRUE)
		{
			hasNodesChanged = I_TRUE;
			fwprintf(fp, L" (recently changed)");
		}
		fwprintf(fp, L"</td><td>");
		switch (pclust->groups[inc]->state)
		{
			case ClusterGroupFailed :
				fwprintf(fp, L"failed");
				break ;
			case ClusterGroupPending :
				fwprintf(fp, L"pending");
				break ;
			case ClusterGroupOnline :
				fwprintf(fp, L"online");
				break ;
			case ClusterGroupPartialOnline :
				fwprintf(fp, L"partial online");
				break ;
			case ClusterGroupOffline :
				fwprintf(fp, L"offline");
				break ;
		}
		fwprintf(fp, L"</td><td>&%s</td>", bbcolors[pclust->groups[inc]->bbState]);
		fwprintf(fp, L"</tr>\n");
	}
	fwprintf(fp, L"</table>\n\n");
	if (hasNodesChanged == I_TRUE)
	{
		fwprintf(fp, L"\n<b>Important Notes :</b>\n\n");
		for (inc = 0; inc < pclust->countGroup; ++inc)
		{
			if (pclust->groups[inc]->nodeHasChanged == I_TRUE)
			{
				fwprintf(fp, L"WARNING !!! active node for group \"%s\" has changed. It was %s and now it is %s.\n", 
					pclust->groups[inc]->name, pclust->groups[inc]->oldNodeName, pclust->groups[inc]->nodeName);
				fwprintf(fp, L"To suppress this warning, please connect to the server and execute the command : <i>erase \"%s\"</i>", pclust->groups[inc]->nodeChangedFilePath);
				fwprintf(fp, L"\n\n");
			}
		}
	}
	fclose(fp);
	wprintf(L"Windows Clustering report has been saved to %s.", lpPath);
	return I_TRUE;
}


int							getTempPath(LPWSTR lpPath)
{
	if (GetTempPathW(BUFFER_SIZE, lpPath) != 0)
		return 1;
	return 0;
}

void						clusterMain()
{
	cluster_t				pcluster;
	
	initCluster(&pcluster);
	//if (checkVersion() == I_FALSE)
	//	fatalError(L"This version of Windows is not supported for Windows Clustering.");
	if (checkClusterState() == I_FALSE)
		exit(1);
	getClusterGroups(&pcluster);
	generateReport(&pcluster);
	freeCluster(&pcluster);
}

int							main ()
{   
	clusterMain();
	return 0;
}
