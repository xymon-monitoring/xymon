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
// $Id: memory.h 48 2007-12-28 10:43:10Z sharpyy $

#ifndef		__MEMORY_H__
#define		__MEMORY_H__

#include "IBBWinAgent.h"


//
// Default alerts values
//
#define DEF_PHYS_WARN	100		/* physical memory */
#define DEF_PHYS_PANIC	101
#define DEF_VIRT_WARN	90		/* virtual memory */
#define DEF_VIRT_PANIC	99
#define DEF_PAGE_WARN	80		/* page-file (swap) */
#define DEF_PAGE_PANIC	90
//#define DEF_REAL_WARN	101		/* real memory use Used Page / Total Physical */
//#define DEF_REAL_PANIC	105

#define	PHYS_MEM_TYPE	0
#define	VIRT_MEM_TYPE	1
#define	PAGE_MEM_TYPE	2
//#define	REAL_MEM_TYPE	3
#define	MAX_MEM_TYPE	3

#define MEM_DIV		1048576

enum BBAlarmType { GREEN, YELLOW, RED };

typedef 	BOOL(__stdcall *MYGETMEMSTATUSEX) (LPMEMORYSTATUSEX);
typedef 	BOOL(__stdcall *MYGETMEMSTATUS) (LPMEMORYSTATUS);


typedef struct 	mem_data_s {
	DWORDLONG  		value;
    DWORDLONG  		total;
    DWORDLONG  		used;
    DWORD			warn;
    DWORD			panic;
    DWORD			color;
}				mem_data_t;

class AgentMemory : public IBBWinAgent
{
	private :
		IBBWinAgentManager 		& m_mgr;
		mem_data_t				m_memData[MAX_MEM_TYPE];
		bool					m_alwaysgreen;
		MEMORYSTATUS 			m_stat;
		MEMORYSTATUSEX 			m_statex;
		MYGETMEMSTATUSEX		m_memStatusEx;
		MYGETMEMSTATUS			m_memStatus;
		bool					m_ready;
		HMODULE  				m_klib;		
		DWORD					m_pageColor;
		std::string				m_testName;
		std::string				m_status;
		
	private :
		void 		SetMemDataLevels(mem_data_t & mem, DWORD warn, DWORD panic);
		bool		GetMemoryData();
		void		ApplyLevels();
		void		SendStatusReport();
		void		SendClientData();
		
	public :
		AgentMemory(IBBWinAgentManager & mgr);
		bool Init();
		void Run();
};


#endif 	// !__MEMORY_H__

