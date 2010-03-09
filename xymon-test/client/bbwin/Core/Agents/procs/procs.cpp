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

#include <iostream>
#include <sstream>
#include <fstream>

#include <string>
#include <map>
using namespace std;

#define BBWIN_AGENT_EXPORTS

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"

using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;

#include "ProcApi.h"

#include "procs.h"

static const BBWinAgentInfo_t 		procsAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"procs",							// agentName;
	"procs agent : check running processes",        // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE		// flags
};                


//
// Simple check rules functions
//
static bool 	EqualProcRule(DWORD cur, DWORD rule) {
	return (cur == rule);
}

static bool 	SupProcRule(DWORD cur, DWORD rule) {
	return (cur > rule);
}

static bool 	SupEqualProcRule(DWORD cur, DWORD rule) {
	return (cur >= rule);
}

static bool 	InfProcRule(DWORD cur, DWORD rule) {
	return (cur < rule);
}

static bool 	InfEqualProcRule(DWORD cur, DWORD rule) {
	return (cur <= rule);
}


//
// common bb colors
//
static const char	*bbcolors[] = {"green", "yellow", "red", NULL};


//
// global rules used for procs
//
const static Rule_t			globalRules[]  = 
{
	{"<=", InfEqualProcRule},
	{"-=", InfEqualProcRule},
	{">=", SupEqualProcRule},
	{"+=", SupEqualProcRule},
	{"==", EqualProcRule},
	{"<", InfProcRule},
	{"-", InfProcRule},
	{"=", EqualProcRule},
	{">", SupProcRule},
	{"+", SupProcRule},
	{NULL, NULL},
};

//
// check if process is running
// it will return the number of instances
// 
static DWORD 		CountProcesses(const string & name) {
	DWORD			count = 0;
	string			match = name;
	string			procname;

	std::transform( match.begin(), match.end(), match.begin(), tolower ); 
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
		PCWSTR pszProcessName = pProcesses->ProcessName.Buffer;
		if (pszProcessName == NULL)
			pszProcessName = L"Idle";
		CHAR szProcessName[MAX_PATH];
		WideCharToMultiByte(CP_ACP, 0, pszProcessName, -1,
			szProcessName, MAX_PATH, NULL, NULL);
		procname = szProcessName;
		std::transform( procname.begin(), procname.end(), procname.begin(), tolower ); 
		size_t	res = procname.find(match);
		if (res >= 0 && res < procname.size()) {
			++count;
		}
		if (pProcesses->NextEntryDelta == 0)
			break ;
		pProcesses = (PSYSTEM_PROCESS_INFORMATION)(((LPBYTE)pProcesses)
			+ pProcesses->NextEntryDelta);
	}
	HeapFree(hHeap, 0, pBuffer);
	return count;
}

//
// only used with the centralized mode
// generate a table with list of process 
//
static void 		ReportProcesses(stringstream & reportData) {
	string									procname;
	DWORD									maxlen = 0;

	reportData << format("PID Name") << endl;
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
		if (pszProcessName == NULL)
			pszProcessName = L"Idle";
		CHAR szProcessName[MAX_PATH];
		WideCharToMultiByte(CP_ACP, 0, pszProcessName, -1,
			szProcessName, MAX_PATH, NULL, NULL);
		procname = szProcessName;
		reportData << format("%-6d %-16s") % pProcesses->ProcessId % procname << endl;
		if (pProcesses->NextEntryDelta == 0)
			break ;
		pProcesses = (PSYSTEM_PROCESS_INFORMATION)(((LPBYTE)pProcesses)
			+ pProcesses->NextEntryDelta);
	}
	HeapFree(hHeap, 0, pBuffer);
}


//
// check processes from the rules configured
// it will return the bbcolor final state
// 0 green
// 1 yellow
// 2 red
// 
BBAlarmType				AgentProcs::ExecProcRules(stringstream & reportData) {
	list<ProcRule_t *>::iterator 	itr;
	stringstream					report;
	BBAlarmType						state;

	state = GREEN;
	for (itr = m_rules.begin(); itr != m_rules.end(); ++itr) {
		DWORD				count = CountProcesses((*itr)->name);
		stringstream		comment;

		if ((*itr)->comment.length() > 0)
			comment << " (" << (*itr)->comment << ")";
		if ((*itr)->apply_rule(count, (*itr)->count)) {
			report << "&green " << (*itr)->name << comment.str() << " " << (*itr)->rule << " - " << count << " instance";
			if (count > 1)
				report << "s";
			report << " running" << endl;
		} else {
			report << "&" << bbcolors[(*itr)->color] << " " << (*itr)->name << comment.str() << " " << (*itr)->rule << " - " << count << " instance";
			if (count > 1)
				report << "s";
			report << " running" << endl;
			if ((*itr)->color > state)
				state = (*itr)->color;
		}
	}
	if (state == GREEN)
		reportData << "All processes are ok\n" << report.str() << endl;
	else
		reportData << "Some processes are in error\n" << endl << report.str() << endl;
	return state;
}


//
// Run method
// called from init
//
void AgentProcs::Run() {
	stringstream 					reportData;	
	BBAlarmType						finalState;
	
    ptime now = second_clock::local_time();
	finalState = GREEN;
	if (m_mgr.IsCentralModeEnabled() == false) {
		reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "] ";
		if (m_rules.size() == 0)
			reportData << "No process to check" << endl;
		else
			finalState = ExecProcRules(reportData);
		reportData << endl;
		m_mgr.Status(m_testName.c_str(), bbcolors[finalState], reportData.str().c_str());
	} else {
		ReportProcesses(reportData);
		m_mgr.ClientData(m_testName.c_str(), reportData.str().c_str());
	}
}

//
// Add rule method
// called from init
//
void AgentProcs::AddRule(const string & name, const string & rule, const string & color, const string & comment) {
	if (name.length() == 0 || rule.length() == 0) {
		m_mgr.ReportEventWarn("some rules are incorrect. Please check your configuration.");
		return ;
	}
	ProcRule_t		*procRule;
	try {
		procRule = new ProcRule_t;
	} catch (std::bad_alloc ex) {
		return ;
	}
	if (procRule == NULL) 
		return ;
	procRule->name = name;
	procRule->color = YELLOW;
	if (color.length() > 0) {
		if (color == "red")
			procRule->color = RED;
	}
	if (comment.length() > 0) {
		procRule->comment = comment;
	}
	procRule->rule = rule;
	procRule->apply_rule = NULL;
	procRule->count = 0;
	for (int i = 0; globalRules[i].apply_rule != NULL; ++i) {
		size_t	res = rule.find(globalRules[i].name);
		if (res >= 0 && res < rule.size()) {
			procRule->apply_rule = globalRules[i].apply_rule;
			procRule->count = m_mgr.GetNbr(rule.substr(strlen(globalRules[i].name), rule.length()).c_str());
			break ;
		}
	}
	if (procRule->apply_rule == NULL) {
		m_mgr.ReportEventWarn("some rules are incorrect. Please check your configuration.");
		delete procRule;
	} else {
		m_rules.push_back(procRule);
	}
}


//
// init function
//
bool AgentProcs::Init() {
	if (m_mgr.IsCentralModeEnabled() == false) {
		PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

		if (conf == NULL)
			return false;
		PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string		name = m_mgr.GetConfigurationRangeValue(range, "name");

			if (name == "testname") {
				string value = 	m_mgr.GetConfigurationRangeValue(range, "value");
				if (value.length() > 0)
					m_testName = value;
			} else {
				AddRule(name.c_str(), 
					m_mgr.GetConfigurationRangeValue(range, "rule"), 
					m_mgr.GetConfigurationRangeValue(range, "alarmcolor"),
					m_mgr.GetConfigurationRangeValue(range, "comment"));
			}
		}
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	return true;
}


//
// constructor 
//
AgentProcs::AgentProcs(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_testName = "procs";
}

//
// Destructor 
//
AgentProcs::~AgentProcs() {
	std::list<ProcRule_t *>::iterator 	itr;
	
	for (itr = m_rules.begin(); itr != m_rules.end(); ++itr) {
		delete (*itr);
	}
	m_rules.clear();
}


//
// common agents export functions
//

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentProcs(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &procsAgentInfo;
}
