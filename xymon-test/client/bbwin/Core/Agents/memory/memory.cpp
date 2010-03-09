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
// $Id: memory.cpp 48 2007-12-28 10:43:10Z sharpyy $

//
// This agent is in part inspired from the original bb_memory
//

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <set>
#include <list>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"
#include "Memory.h"

using namespace std;
using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;


static const char *bbcolors[] = { "green", "yellow", "red", NULL };


static const BBWinAgentInfo_t 		memoryAgentInfo =
{
	BBWIN_AGENT_VERSION,					// bbwinVersion;
	"memory",								// agentName;
	"memory agent : report memory usage",	// agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE		// flags
};                

bool		AgentMemory::GetMemoryData() {
	m_klib = GetModuleHandle("kernel32.dll");
    if (m_klib == NULL) {
		m_mgr.ReportEventError("can't get kernel32.dll module");
		return false;
	}
	m_ready = false;
    m_memStatusEx = (MYGETMEMSTATUSEX)GetProcAddress(m_klib, "GlobalMemoryStatusEx");
	if (m_memStatusEx != NULL) {
		m_statex.dwLength = sizeof(m_statex);
		m_memStatusEx(&m_statex);
	    m_ready = true;
    }
	if (m_ready == false) {
		m_memStatus = (MYGETMEMSTATUS)GetProcAddress(m_klib, "GlobalMemoryStatus");
		if (m_memStatus != NULL) {
			MEMORYSTATUS 		memCopy;

			memCopy.dwLength = sizeof(memCopy);
            (m_memStatus)(&memCopy);
            m_statex.dwLength = memCopy.dwLength;
            m_statex.dwMemoryLoad = 0;
            m_statex.ullTotalPhys = memCopy.dwTotalPhys;
            m_statex.ullAvailPhys = memCopy.dwAvailPhys;
            m_statex.ullTotalPageFile = memCopy.dwTotalPageFile;
            m_statex.ullAvailPageFile = memCopy.dwAvailPageFile;
            m_statex.ullTotalVirtual = memCopy.dwTotalVirtual;
            m_statex.ullAvailVirtual = memCopy.dwAvailVirtual;
            m_ready = true;
		}
    }
	if (m_ready == false) {
		m_mgr.ReportEventError("can't find GlobalMemoryStatus");
		return false;
	}
	m_statex.ullTotalPhys /= MEM_DIV;
    m_statex.ullAvailPhys /= MEM_DIV;
    m_statex.ullTotalPageFile /= MEM_DIV;
    m_statex.ullAvailPageFile /= MEM_DIV;
    m_statex.ullTotalVirtual /= MEM_DIV;
    m_statex.ullAvailVirtual /= MEM_DIV;
    m_memData[PHYS_MEM_TYPE].total = m_statex.ullTotalPhys;
    m_memData[PHYS_MEM_TYPE].used  = m_statex.ullTotalPhys - m_statex.ullAvailPhys;
    m_memData[PHYS_MEM_TYPE].value = (m_memData[PHYS_MEM_TYPE].used * 100) / m_statex.ullTotalPhys;
    m_memData[VIRT_MEM_TYPE].total = m_statex.ullTotalVirtual;
    m_memData[VIRT_MEM_TYPE].used  = m_statex.ullTotalVirtual - m_statex.ullAvailVirtual;
    m_memData[VIRT_MEM_TYPE].value = (m_memData[VIRT_MEM_TYPE].used * 100) / m_statex.ullTotalVirtual;
    m_memData[PAGE_MEM_TYPE].total = m_statex.ullTotalPageFile;
    m_memData[PAGE_MEM_TYPE].used  = m_statex.ullTotalPageFile - m_statex.ullAvailPageFile;
    m_memData[PAGE_MEM_TYPE].value = (m_memData[PAGE_MEM_TYPE].used * 100) / m_statex.ullTotalPageFile;
	/* calculate the real memory usage */
	//if (m_memData[PHYS_MEM_TYPE].total != 0)
	//	m_memData[REAL_MEM_TYPE].value = (DWORDLONG)(((float)m_memData[PAGE_MEM_TYPE].used / (float)m_memData[PHYS_MEM_TYPE].total) * (float)100.00);
	return true;
}

void		AgentMemory::ApplyLevels() {
	m_pageColor = GREEN;
	for (DWORD inc = 0; inc < MAX_MEM_TYPE; ++inc) {
		m_memData[inc].color = GREEN;
		if (m_memData[inc].value >= m_memData[inc].warn) {
			m_memData[inc].color = YELLOW;
			if (m_pageColor == GREEN) {
				m_pageColor = YELLOW;
				m_status = "Memory low";
			}
		}
		if (m_memData[inc].value >= m_memData[inc].panic) {
			m_memData[inc].color = RED;
			if (m_pageColor == GREEN || m_pageColor == YELLOW) {
				m_pageColor = RED;
				m_status = "Memory **very** low";
			}
		}
	}
}

void		AgentMemory::SendStatusReport() {
	stringstream 			reportData;	
	
    ptime now = second_clock::local_time();
	reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "] " << m_status << endl;
	reportData << endl;
	reportData << format("    Memory  %6s  %6s  %s\n") % "Used" % "Total" % "Pctg";
	reportData << format("&%s Physical:  %6luM %6luM  %3lu%%\n") % bbcolors[m_memData[PHYS_MEM_TYPE].color] %
			m_memData[PHYS_MEM_TYPE].used % m_memData[PHYS_MEM_TYPE].total % m_memData[PHYS_MEM_TYPE].value;
	reportData << format("&%s Virtual:   %6luM %6luM  %3lu%%\n") % bbcolors[m_memData[VIRT_MEM_TYPE].color] %
			m_memData[VIRT_MEM_TYPE].used % m_memData[VIRT_MEM_TYPE].total % m_memData[VIRT_MEM_TYPE].value;
	reportData << format("&%s Page:      %6luM %6luM  %3lu%%\n") % bbcolors[m_memData[PAGE_MEM_TYPE].color] %
			m_memData[PAGE_MEM_TYPE].used % m_memData[PAGE_MEM_TYPE].total % m_memData[PAGE_MEM_TYPE].value;
	m_mgr.Status(m_testName.c_str(), bbcolors[m_pageColor], reportData.str().c_str());
}

void		AgentMemory::SendClientData() {
	stringstream 			reportData;	

	reportData << format("memory  %6s  %6s\n") % "Total" % "Used";
	reportData << format("physical:  %6lu %6lu\n") % m_memData[PHYS_MEM_TYPE].total % m_memData[PHYS_MEM_TYPE].used;
	reportData << format("virtual:   %6lu %6lu\n") % m_memData[VIRT_MEM_TYPE].total % m_memData[VIRT_MEM_TYPE].used;
	reportData << format("page:      %6lu %6lu\n") % m_memData[PAGE_MEM_TYPE].total % m_memData[PAGE_MEM_TYPE].used;
	m_mgr.ClientData(m_testName.c_str(), reportData.str().c_str());
}

void 		AgentMemory::Run() {
	if (GetMemoryData() == false)
		return ;
	if (m_mgr.IsCentralModeEnabled() == false) {
		ApplyLevels();
		if (m_alwaysgreen == true) {
			m_pageColor = GREEN;
			m_status = "Memory AlwaysGreen";
		}
		SendStatusReport();
	} else {
		SendClientData();
	}
}

void			AgentMemory::SetMemDataLevels(mem_data_t & mem, DWORD warn, DWORD panic) {
	if (warn > 0 && warn < 110)
		mem.warn = warn;
	else
		m_mgr.ReportEventWarn("invalid warn range level. (1 to 101 % needed)");
	if (panic > 0 && panic < 110)
		mem.panic = panic;
	else {
		m_mgr.ReportEventWarn("invalid panic range level. (1 to 101 % needed)");
	}
	if (mem.panic < mem.warn) {
		m_mgr.ReportEventWarn("one warn level is higher than the panic level");
		mem.warn = mem.panic;
	}
}

bool AgentMemory::Init() {
	if (m_mgr.IsCentralModeEnabled() == false) {
		PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

		if (conf == NULL)
			return false;
		m_klib = GetModuleHandle("kernel32.dll");
		if (m_klib == NULL) {
			m_mgr.ReportEventError("can't get kernel32.dll module");
			return false;
		}
		PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string			name;

			name = m_mgr.GetConfigurationRangeValue(range, "name");
			if (name == "alwaysgreen") {
				string value =  m_mgr.GetConfigurationRangeValue(range, "value");
				if (value == "true")
					m_alwaysgreen = true;
			} else if (name == "testname") {
				string value =  m_mgr.GetConfigurationRangeValue(range, "value");
				if (value.length() > 0)
					m_testName = value;
			} else {
				DWORD panic, warn;
				string sPanic, sWarn;

				sPanic = m_mgr.GetConfigurationRangeValue(range, "paniclevel");
				sWarn = m_mgr.GetConfigurationRangeValue(range, "warnlevel");
				if (name == "physical") {
					warn = (sWarn == "") ? DEF_PHYS_WARN : m_mgr.GetNbr(sWarn.c_str());
					panic = (sPanic == "") ? DEF_PHYS_PANIC : m_mgr.GetNbr(sPanic.c_str());
					SetMemDataLevels(m_memData[PHYS_MEM_TYPE], warn, panic);
				}
				if (name == "page") {
					warn = (sWarn == "") ? DEF_PAGE_WARN : m_mgr.GetNbr(sWarn.c_str());
					panic = (sPanic == "") ? DEF_PAGE_PANIC : m_mgr.GetNbr(sPanic.c_str());
					SetMemDataLevels(m_memData[PAGE_MEM_TYPE], warn, panic);
				}
				if (name == "virtual") {
					warn = (sWarn == "") ? DEF_VIRT_WARN : m_mgr.GetNbr(sWarn.c_str());
					panic = (sPanic == "") ? DEF_VIRT_PANIC : m_mgr.GetNbr(sPanic.c_str());
					SetMemDataLevels(m_memData[VIRT_MEM_TYPE], warn, panic);
				}
				//if (name == "real") {
				//	warn = (sWarn == "") ? DEF_REAL_WARN : m_mgr.GetNbr(sWarn.c_str());
				//	panic = (sPanic == "") ? DEF_REAL_PANIC : m_mgr.GetNbr(sPanic.c_str());
				//	SetMemDataLevels(m_memData[REAL_MEM_TYPE], warn, panic);
				//}
			}
		}
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	return true;
}

AgentMemory::AgentMemory(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_alwaysgreen = false;
	ZeroMemory(m_memData, sizeof(m_memData));
	m_memData[PHYS_MEM_TYPE].warn = DEF_PHYS_WARN;
	m_memData[PHYS_MEM_TYPE].panic = DEF_PHYS_PANIC;
	m_memData[VIRT_MEM_TYPE].warn = DEF_VIRT_WARN;
	m_memData[VIRT_MEM_TYPE].panic = DEF_VIRT_PANIC;
	m_memData[PAGE_MEM_TYPE].warn = DEF_PAGE_WARN;
	m_memData[PAGE_MEM_TYPE].panic = DEF_PAGE_PANIC;
	m_testName = "memory";
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentMemory(mgr);
}

BBWIN_AGENTDECL void DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &memoryAgentInfo;
}
