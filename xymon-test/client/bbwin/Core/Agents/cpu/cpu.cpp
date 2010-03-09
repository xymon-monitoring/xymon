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

#include <windows.h>
#include <WtsApi32.h>

#include <set>
#include <list>
#include <iostream>
#include <sstream>
#include <fstream>

#include <string>

using namespace std;

#include "SystemCounters.h"


#define BBWIN_AGENT_EXPORTS

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"

using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;

#include "ProcApi.h"

#include "CpuUsage.h"
#include "cpu.h"


static const char *bbcolors[] = { "green", "yellow", "red", NULL };


static const BBWinAgentInfo_t 		cpuAgentInfo =
{
	BBWIN_AGENT_VERSION,					// bbwinVersion;
	"cpu",    								// agentName;
	"cpu agent : report cpu usage",        	// agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE		// flags
};                


//
// return the number of processor
// return 1 if the information can't be resolved
//
static DWORD	CountProcessor() {
	HKEY 		hKey;
	DWORD 		count;
	
	count = 0;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("HARDWARE\\DESCRIPTION\\System\\CentralProcessor"), 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &hKey) != ERROR_SUCCESS) {		
		return 1;
	}
	RegQueryInfoKey(hKey, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	RegCloseKey(hKey);
	if (count == 0)
		count = 1;
	return count;
}

//
// return count number
//
static DWORD 		countProcesses() {
	DWORD			count = 0;

	HINSTANCE hNtDll;
	NTSTATUS (WINAPI * pZwQuerySystemInformation)(UINT, PVOID, ULONG, PULONG);
	hNtDll = GetModuleHandle("ntdll.dll");
	assert(hNtDll != NULL);
	// find ZwQuerySystemInformation address
	*(FARPROC *)&pZwQuerySystemInformation =
		GetProcAddress(hNtDll, "ZwQuerySystemInformation");
	if (pZwQuerySystemInformation == NULL)
		return 0;
	HANDLE hHeap = GetProcessHeap();
	NTSTATUS Status;
	ULONG cbBuffer = 0x8000;
	PVOID pBuffer = NULL;
	// it is difficult to predict what buffer size will be
	// enough, so we start with 32K buffer and increase its
	// size as needed
	do
	{
		pBuffer = HeapAlloc(hHeap, 0, cbBuffer);
		if (pBuffer == NULL)
			return 0;
		Status = pZwQuerySystemInformation(
			SystemProcessesAndThreadsInformation,
			pBuffer, cbBuffer, NULL);
		if (Status == STATUS_INFO_LENGTH_MISMATCH) {
			HeapFree(hHeap, 0, pBuffer);
			cbBuffer *= 2;
		}
		else if (!NT_SUCCESS(Status)) {
			HeapFree(hHeap, 0, pBuffer);
			return 0;
		}
	}
	while (Status == STATUS_INFO_LENGTH_MISMATCH);
	PSYSTEM_PROCESS_INFORMATION pProcesses = 
		(PSYSTEM_PROCESS_INFORMATION)pBuffer;
	for (;;) {
		++count;
		if (pProcesses->NextEntryDelta == 0)
			break ;
		pProcesses = (PSYSTEM_PROCESS_INFORMATION)(((LPBYTE)pProcesses)
			+ pProcesses->NextEntryDelta);
	}
	HeapFree(hHeap, 0, pBuffer);
	return count;
}

//**********************************
//
// UsageProc class Methods
//
//**********************************

//
// constructor
UsageProc::UsageProc(DWORD pid) {
	m_pid = pid;
	m_exists = false;
	m_lastUsage = 0.00;
	m_mem = 0;
	m_priority = 0;
}

//
// Get Usage function
//
double			UsageProc::ExecGetUsage() {
	m_lastUsage = m_usage.GetCpuUsage(m_pid);
	return m_lastUsage;
}


//**********************************
//
// CPU agent class Methods
//
//**********************************

void 		AgentCpu::GetProcsOwners() {
	
	procs_itr			itr;
	PWTS_PROCESS_INFO	ppProcessInfom = NULL;
	DWORD				count;
	TCHAR				userbuf[ACCOUNT_SIZE];
	TCHAR				domainbuf[ACCOUNT_SIZE];
	DWORD				userSize, domainSize;
	SID_NAME_USE		type;
	
	if (m_useWts == false)
		return;
	count = 0;
	BOOL res = m_WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &ppProcessInfom, &count);
	if (res == 0) 
		return ;
	assert(ppProcessInfom != NULL);
	for (DWORD inc = 0; inc < count; ++inc) {
		userSize = ACCOUNT_SIZE;
		domainSize = ACCOUNT_SIZE;
		SecureZeroMemory(userbuf, ACCOUNT_SIZE);
		SecureZeroMemory(domainbuf, ACCOUNT_SIZE);
		BOOL res = LookupAccountSid(NULL, ppProcessInfom[inc].pUserSid, userbuf, &userSize,
			  domainbuf, &domainSize, &type);
		if (res) {
			stringstream 		username;	

			username << domainbuf << "\\" << userbuf;
			itr = m_procs.find(ppProcessInfom[inc].ProcessId);
			if (itr != m_procs.end()) {
				(*itr).second->SetOwner(username.str().c_str());
			}
		} else {
			itr = m_procs.find(ppProcessInfom[inc].ProcessId);
			if (itr != m_procs.end()) {
				(*itr).second->SetOwner("unknown");
			}
		}
	}
	m_WTSFreeMemory(ppProcessInfom);
}

void 		AgentCpu::GetProcsData() {
	string				procname;
	UsageProc			*proc;
	procs_itr			itr;

	HINSTANCE hNtDll;
	NTSTATUS (WINAPI * pZwQuerySystemInformation)(UINT, PVOID, ULONG, PULONG);
	hNtDll = GetModuleHandle("ntdll.dll");
	assert(hNtDll != NULL);
	// find ZwQuerySystemInformation address
	*(FARPROC *)&pZwQuerySystemInformation =
		GetProcAddress(hNtDll, "ZwQuerySystemInformation");
	if (pZwQuerySystemInformation == NULL)
		return ;
	HANDLE hHeap = GetProcessHeap();
	NTSTATUS Status;
	ULONG cbBuffer = 0x8000;
	PVOID pBuffer = NULL;
	// it is difficult to predict what buffer size will be
	// enough, so we start with 32K buffer and increase its
	// size as needed
	do
	{
		pBuffer = HeapAlloc(hHeap, 0, cbBuffer);
		if (pBuffer == NULL)
			return ;
		Status = pZwQuerySystemInformation(
			SystemProcessesAndThreadsInformation,
			pBuffer, cbBuffer, NULL);
		if (Status == STATUS_INFO_LENGTH_MISMATCH) {
			HeapFree(hHeap, 0, pBuffer);
			cbBuffer *= 2;
		}
		else if (!NT_SUCCESS(Status)) {
			HeapFree(hHeap, 0, pBuffer);
			return ;
		}
	}
	while (Status == STATUS_INFO_LENGTH_MISMATCH);
	PSYSTEM_PROCESS_INFORMATION pProcesses = 
		(PSYSTEM_PROCESS_INFORMATION)pBuffer;
	for (;;) {
		PCWSTR pszProcessName = pProcesses->ProcessName.Buffer;
		if (pszProcessName == NULL) {
			if (pProcesses->NextEntryDelta == 0)
				break ;
			pProcesses = (PSYSTEM_PROCESS_INFORMATION)(((LPBYTE)pProcesses)
			+ pProcesses->NextEntryDelta);
			continue ;
		}
		CHAR szProcessName[MAX_PATH];
		WideCharToMultiByte(CP_ACP, 0, pszProcessName, -1,
			szProcessName, MAX_PATH, NULL, NULL);
		procname = szProcessName;
		itr = m_procs.find(pProcesses->ProcessId);
		if (itr == m_procs.end()) {
			try {
				proc = new UsageProc(pProcesses->ProcessId);
			} catch (std::bad_alloc ex) {
				m_mgr.Log(LOGLEVEL_ERROR, "Can't alloc memory");
				continue ;
			}
			if (proc == NULL) {
				m_mgr.Log(LOGLEVEL_ERROR, "Can't alloc memory");
				continue ;
			}
			proc->SetName(procname);
			m_procs.insert(pair<DWORD, UsageProc *> (pProcesses->ProcessId, proc));
		} else {
			proc = itr->second;
		}
		proc->SetMemUsage(pProcesses->VmCounters.WorkingSetSize / 1024);
		proc->ExecGetUsage();
		proc->SetPriority(pProcesses->BasePriority);
		proc->SetExists(true);
		proc->SetCpuTime(((int)((pProcesses->KernelTime.QuadPart / 10000000) + (pProcesses->UserTime.QuadPart / 10000000))));
		if (pProcesses->NextEntryDelta == 0)
			break ;
		pProcesses = (PSYSTEM_PROCESS_INFORMATION)(((LPBYTE)pProcesses)
			+ pProcesses->NextEntryDelta);
	}
	HeapFree(hHeap, 0, pBuffer);
}

static const std::string BuildCpuTimeString(int time) {
	int					h, m, s;
	stringstream 		stime;	

	h = time / 3600;
	time -= h * 3600;
	m = time / 60;
	time -= m * 60;
	s = time;
	stime << format("%u:%02u:%02u") % h % m % s;
	return stime.str();
}

//
// report sending method
//
void		AgentCpu::SendStatusReport() {
	stringstream 		reportData;	
	CSystemCounters		data;
	DWORD				totalUsage = 0;

	m_pageColor = GREEN;
	totalUsage = ceil(m_usage.usageVal);
	if (totalUsage > 100) 
		totalUsage = 100;
	if (m_mgr.IsCentralModeEnabled() == false) {
		ptime now = second_clock::local_time();
		date today = now.date();
		reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "] ";
	}
	reportData << "up: " << (data.GetSystemUpTime() / 86400) << " days, ";
	reportData << data.GetServerSessions() << " users, ";
	reportData << countProcesses() << " procs, ";
	reportData << "load=" << totalUsage << "%";
	reportData << "\n\n" << endl;	
	if (m_mgr.IsCentralModeEnabled() == false && m_uptimeMonitoring && m_uptimeDelay >= data.GetSystemUpTime()) {
		reportData << "&yellow Warning: Machine recently rebooted\n\n" << endl;
		m_pageColor = YELLOW;
	}
	if (m_psMode) {
		DWORD				count = 0;
		procs_sorted_itr_t	itrSort;
		
		reportData << "CPU states: " << endl;
		reportData << "     total" << "  " << format("%02u%%") % totalUsage << endl;
		for (int pc = 0; pc < m_procCount; ++pc) {
			reportData << "     cpu" << format("%02u") % pc << "  " << format("%02.01f%%") % m_usageProc[pc].usageVal << endl;
		}
		reportData << endl;
		if (m_limit != 0)
			reportData << "Information : ps mode is limited to " << m_limit << " lines\n" << endl;
		reportData << format("CPU    PID   %-16s  Pri  %-8s") % "Image Name" % "Time";
		if (m_useWts)
			reportData << format(" %-35s") % "Owner";
		reportData << " MemUsage" << endl;
		for (itrSort = m_procsSorted.begin(); itrSort != m_procsSorted.end(); ++itrSort) {
			if ((*itrSort)->GetUsage() < 10)
				reportData << "0";
			reportData << format("%02.01f%%  %-4u  %-16s  %-3u  %-8s") % ((*itrSort)->GetUsage() / m_procCount) %  (*itrSort)->GetPid() % (*itrSort)->GetName() 
					% (*itrSort)->GetPriority() % BuildCpuTimeString((*itrSort)->GetCpuTime());
			if (m_useWts)
				reportData << format(" %-35s") % (*itrSort)->GetOwner();
			reportData << format(" %luk") % (*itrSort)->GetMemUsage() << endl;
			if (m_limit != 0 && count >= (m_limit - 1)) {
				reportData << "..." << endl;
				break ;
			}
			++count;
		}
	}
	reportData << endl;
	if (m_mgr.IsCentralModeEnabled() == false) {
		if (totalUsage >= m_warnPercent && totalUsage < m_panicPercent) {
			if (m_curDelay >= m_delay)
				m_pageColor = YELLOW;
			++m_curDelay;
		} else if (totalUsage >= m_panicPercent) {
			if (m_curDelay >= m_delay)
				m_pageColor = RED;
			++m_curDelay;
		} else { // reset delay
			m_curDelay = 0;
		}
		if (m_alwaysgreen)
			m_pageColor = GREEN;
		m_mgr.Status(m_testName.c_str(), bbcolors[m_pageColor], reportData.str().c_str());	
	} else {
		m_mgr.ClientData(m_testName.c_str(), reportData.str().c_str());	
	}
}

//
// Delete older procs
//
void		AgentCpu::DeleteOlderProcs() {	
	procs_itr				itr;
	list<DWORD>				toDelete;
	list<DWORD>::iterator	itrDel;
	
	for (itr = m_procs.begin(); itr != m_procs.end(); ++itr) {
		if (itr->second->GetExists() == false) {
			toDelete.push_back(itr->first);
		}
	}
	for (itrDel = toDelete.begin(); itrDel != toDelete.end(); ++itrDel) {
		delete m_procs[(*itrDel)];
		m_procs.erase((*itrDel));
	}
}

void		AgentCpu::InitProcs() {	
	procs_itr			itr;
	
	for (itr = m_procs.begin(); itr != m_procs.end(); ++itr) {
		itr->second->SetExists(false);
	}
}

void		AgentCpu::GetCpuData() {
	if (m_psMode) {
		GetProcsData();
	}
	// total cpu usage
	m_usage.usageVal = m_usage.usageObj.GetCpuUsage() / m_procCount;
	// cpu usage by processor
	for (int pc = 0; pc < m_procCount; ++pc) {
		m_usageProc[pc].usageVal = m_usageProc[pc].usageObj.GetCpuUsage();
	}
	// if processor count is different than 1, then we used values from other processors 
	// to calculate the total cpu usage
	if (m_procCount > 1) {
		m_usage.usageVal = 0.00;
		for (int pc = 0; pc < m_procCount; ++pc) {
			m_usage.usageVal += m_usageProc[pc].usageVal;
		}
		m_usage.usageVal /= m_procCount;
	}
}

//
// Main running Method
//
void AgentCpu::Run() {
	procs_itr			itr;
	
	m_mgr.Log(LOGLEVEL_DEBUG, "cpu review started");
	if (m_psMode)
		InitProcs();
	GetCpuData();
	if (m_psMode) {
		DeleteOlderProcs();
		for (itr = m_procs.begin(); itr != m_procs.end(); ++itr) {
			m_procsSorted.insert(itr->second);
		}
		GetProcsOwners();
	}
	SendStatusReport();
	m_procsSorted.clear();
	m_firstPass = false;
	m_mgr.Log(LOGLEVEL_DEBUG, "cpu review ended");
}

//
// destructor
//
AgentCpu::~AgentCpu() {
	procs_itr			itr;
	
	for (itr = m_procs.begin(); itr != m_procs.end(); ++itr) {
		delete (itr->second);
	}
	m_procs.clear();
	if (m_mWts) {
		FreeLibrary(m_mWts);
		m_mWts = NULL;
	}
	delete [] m_usageProc;
}


bool AgentCpu::Init() {
	PBBWINCONFIG		conf;
	
	m_mgr.Log(LOGLEVEL_DEBUG, "initialization cpu agent started");
	if (m_mgr.IsCentralModeEnabled() == false) {
		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());
		if (conf == NULL) {
			return false;
		}
		PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string		name, value;

			name = m_mgr.GetConfigurationRangeValue(range, "name");
			value = m_mgr.GetConfigurationRangeValue(range, "value");
			if (name == "alwaysgreen" && value == "true") {
				m_alwaysgreen = true;
			}
			if (name == "psmode" && value == "false") {
				m_psMode = false;
			}
			if (name == "testname" && value.length() > 0) {
				m_testName = value;
			}
			if (name == "limit" && value.length() >0) {
				m_limit = m_mgr.GetNbr(value.c_str());
			}
			if (name == "uptime" && value.length()) {
				m_uptimeMonitoring = true;
				m_uptimeDelay = m_mgr.GetSeconds(value.c_str());
			}
			if (name == "default") {
				string		warn, panic, delay;

				warn = m_mgr.GetConfigurationRangeValue(range, "warnlevel");
				panic = m_mgr.GetConfigurationRangeValue(range, "paniclevel");
				delay = m_mgr.GetConfigurationRangeValue(range, "delay");
				if (warn.size() > 0)
					m_warnPercent = m_mgr.GetNbr(warn.c_str());
				if (panic.size() > 0)
					m_panicPercent = m_mgr.GetNbr(panic.c_str());
				if (delay.size() > 0)
					m_delay = m_mgr.GetNbr(delay.c_str());
			}
		}
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	InitWtsExtension();
	m_mgr.Log(LOGLEVEL_DEBUG, "initialization cpu agent ended");
	return true;
}

void			AgentCpu::InitWtsExtension() {
	m_mWts = LoadLibrary("Wtsapi32.dll");
	if (m_mWts == NULL) {
		// no use of the wts extension
		return ;
	}
	m_WTSEnumerateProcesses = (WTSEnumerateProcesses_t)GetProcAddress(m_mWts, "WTSEnumerateProcessesA");
	m_WTSFreeMemory = (WTSFreeMemory_t)GetProcAddress(m_mWts, "WTSFreeMemory");
	if (m_WTSEnumerateProcesses && m_WTSFreeMemory)
		m_useWts = true;
}

AgentCpu::AgentCpu(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_procCount = CountProcessor();
	try {
		m_usageProc = new myCCpuUsage[m_procCount];
		for (int count = 0; count < m_procCount; ++count) {
			m_usageProc[count].usageObj.SetProcessorIndex(count);
		}
	} catch (std::bad_alloc ex) {
		m_mgr.Log(LOGLEVEL_ERROR, "AgentCpu::AgentCpu Can't alloc memory");
	}
	m_alwaysgreen = false;
	m_firstPass = true;
	m_psMode = true;
	m_warnPercent = DEF_CPU_WARN;
	m_panicPercent = DEF_CPU_PANIC;
	m_delay = DEF_CPU_DELAY;
	m_curDelay = 0;
	m_testName = "cpu";
	m_useWts = false;
	m_WTSEnumerateProcesses = NULL;
	m_WTSFreeMemory = NULL;
	m_limit = 0;
	m_uptimeMonitoring = false;
	m_uptimeDelay = 30 * 60;
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentCpu(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &cpuAgentInfo;
}
