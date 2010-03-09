//this file is part of BBWin
//Copyright (C)2006 Etienne GRIGNON  ( etienne.grignon@gmail.com )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Id: disk.h 32 2007-12-20 17:56:28Z sharpyy $

#ifndef		__DISK_H__
#define		__DISK_H__

#include "IBBWinAgent.h"

/*
** Defines
*/ 

// Somes usefull defines
#define BUFFER_SIZE 		2046
#define MAXNAMELEN			512
#define GB_DIV				(1024 * 1024)
#define MAX_DRIVE			64

#define DEF_DISK_WARN	"90%"
#define DEF_DISK_PANIC	"95%"

#define DEFAULT_RULE_NAME		"default"


// volume management
typedef 	HANDLE (__stdcall *MYFINDFIRSTVOLUMEMOUNTPOINT) (LPTSTR, LPTSTR, DWORD);
typedef 	BOOL(__stdcall *MYFINDNEXTVOLUMEMOUNTPOINT) (HANDLE, LPTSTR, DWORD);
typedef		BOOL(__stdcall *MYFINDVOLUMEMOUNTPOINTCLOSE) (HANDLE);


//
// simple struct to store disk rules
// from the config file
//
typedef struct		disk_rule_s
{
	bool			ignore;
	__int64			warnSize;
	DWORD			warnPercent;
	bool			warnUseSize;
	__int64			panicSize;
	DWORD			panicPercent;
	bool			panicUseSize;
	bool			used; // flag to check that rules are used
}					disk_rule_t;

typedef struct		disk_unit_s
{	
	char			*name;
	__int64			size;
}					disk_unit_t;

typedef struct		disk_type_s
{
	char			*name;
	UINT			type;
}					disk_type_t;


//
// disk structure filled to get disk information
//
typedef struct		disk_s
{
	TCHAR			letter[8];
	TCHAR			fileSystemName[MAXNAMELEN + 1];
	TCHAR			volumeName[MAXNAMELEN + 1];
	DWORD			fileSystemFlags;
	DWORD			maximumComponentLength;
	DWORD 			volumeSerialNumber;
	DWORD			type;
	__int64 		i64FreeBytesToCaller;
	__int64			i64TotalBytes;
	__int64			i64FreeBytes;
	DWORD			color;
	DWORD			percent;
	bool			ignore;
}					disk_t;

enum BBAlarmType { GREEN, YELLOW, RED };

class AgentDisk : public IBBWinAgent
{
	private :
		IBBWinAgentManager 						& m_mgr;
		bool									m_checkRemote;
		bool									m_checkCdrom;
		bool									m_alwaysgreen; 
		DWORD									m_pageColor;
		std::map<std::string, disk_rule_t *>	m_rules;
		std::list<disk_t *>						m_disks;
		std::string								m_testName;
		MYFINDFIRSTVOLUMEMOUNTPOINT				m_findFirstVolumeMountPoint;
		MYFINDNEXTVOLUMEMOUNTPOINT				m_findNextVolumeMountPoint;
		MYFINDVOLUMEMOUNTPOINTCLOSE				m_findVolumeMountPointClose;

	private :
		void 		AddRule(const std::string & label, const std::string & warnlevel, 
								const std::string & paniclevel,	const std::string & ignore);
		void		BuildRule(disk_rule_t & rule, const std::string & warnlevel, const std::string & paniclevel);
		void		GenerateSummary(const disk_t & disk, std::stringstream & summary);
		bool		GetSizeValue(const std::string & level, __int64 & val);
		void		GetMountPointData(LPTSTR driveName);
		bool		GetDisksData();
		void		FreeDisksData();
		void		ApplyRules();
		DWORD		ApplyRule(disk_t & disk);
		void		SendStatusReport();
		
	public :
		AgentDisk(IBBWinAgentManager & mgr);
		~AgentDisk();
		bool Init();
		void Run();
};


#endif 	// !__DISK_H__
