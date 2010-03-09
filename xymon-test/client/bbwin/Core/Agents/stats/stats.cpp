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
// $Id: stats.cpp 63 2008-01-21 13:30:25Z sharpyy $

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <iphlpapi.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"
#include "Stats.h"

using namespace std;
using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;

static const BBWinAgentInfo_t 		statsAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"stats",							// agentName;
	"stats agent :  report general stats for trends purpose",        // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE	// flags
};                

#define		TEMP_PATH_LEN		1024

void				AgentStats::IfStat(stringstream & reportData) {
	PMIB_IFTABLE		piftable;
	PMIB_IPADDRTABLE	piptable;
	PMIB_IPADDRROW		piprow;
	DWORD				dwSize = 0;

	GetIfTable(NULL, &dwSize, FALSE);
	if (piftable = (PMIB_IFTABLE)GlobalAlloc(GMEM_FIXED, dwSize)) {
		if (GetIfTable(piftable, &dwSize, FALSE) == NO_ERROR) {
			for (DWORD inc = 0; inc < piftable->dwNumEntries; inc++) {
				///* IP          Ibytes Obytes */
				///* 192.168.0.1 1818   1802  */
				//static const char *ifstat_bbwin_exprs[] = {
				//"^([a-zA-Z0-9.:]+)\\s+([0-9]+)\\s+([0-9]+)"
				//};

				// interfaces with null mac ignored
				if ((piftable->table[inc].bPhysAddr[0] + piftable->table[inc].bPhysAddr[1] + 
					piftable->table[inc].bPhysAddr[2] + piftable->table[inc].bPhysAddr[3] + piftable->table[inc].bPhysAddr[4]
				+ piftable->table[inc].bPhysAddr[5]) == 0)
					continue ;
				GetIpAddrTable(NULL, &dwSize, FALSE);
				// interfaces with null IP ignored
				if (piptable = (PMIB_IPADDRTABLE)GlobalAlloc(GMEM_FIXED, dwSize)) {
					GetIpAddrTable(piptable, &dwSize, FALSE);
					piprow = &piptable->table[inc];
					if ((piprow->dwAddr & 0xFF) == 0 
					&& ((piprow->dwAddr >> 8) & 0xFF) == 0 
					&& ((piprow->dwAddr >> 16) & 0xFF) == 0
					&& ((piprow->dwAddr >> 24) & 0xFF) == 0) {
						GlobalFree(piptable);
						continue ;
					}
					reportData << format("%u.%u.%u.%u") %	(piprow->dwAddr & 0xFF) %
															((piprow->dwAddr >> 8) & 0xFF) %
															((piprow->dwAddr >> 16) & 0xFF) %
															((piprow->dwAddr >> 24) & 0xFF);
					GlobalFree(piptable);
				}
				reportData << format(" %lu %lu") % piftable->table[inc].dwInOctets 
												% piftable->table[inc].dwOutOctets << endl;
			}
		}
		GlobalFree(piftable);
	}
}

static	int			MyNetstatExec(const string & cmd, const string & path) {
	string			execCmd;

	execCmd = cmd + " > " + path;
	return system(execCmd.c_str());
}

void	AgentStats::NetstatLocal(const string & path, stringstream & reportData) {
	MyNetstatExec((string)"netstat -s", path);
	ifstream netstatFile(path.c_str());
	if (netstatFile) {
		string 		line;
		size_t		ret;
		string 		prefix;

		while (getline(netstatFile, line)) {
			if ((ret = line.find("IP Statistics")) >= 0 && ret < line.size()) {
				prefix = "ip";
				reportData << endl;
			} else if ((ret = line.find("ICMP Statistics")) >= 0 && ret < line.size()) {
				prefix = "icmp";
				reportData << endl;
			} else if ((ret = line.find("TCP Statistics")) >= 0 && ret < line.size()) {
				prefix = "tcp";
				reportData << endl;
			} else if ((ret = line.find("UDP Statistics")) >= 0 && ret < line.size()) {
				prefix = "udp";
				reportData << endl;
			} else if ((ret = line.find("=")) >= 0 && ret < line.size()) {
				reportData << prefix;
				for (size_t i = 0; i < line.size(); ++i) {
					if (line[i] != ' ' && line[i] != '\t')
						reportData << line[i];
				}
				reportData << endl;
			}
		}
	}
	reportData << endl;
}

void	AgentStats::NetstatCentralizedStep(const string & cmd, const string & path, const string dataName) {
	stringstream 	reportData;	

	if (MyNetstatExec(cmd, path)) {
		string err = (string)"failed to execute " + cmd;
		m_mgr.Log(LOGLEVEL_ERROR,err.c_str());
		m_mgr.ReportEventError(err.c_str());
	}
	ifstream fileOut(path.c_str());
	if (fileOut) {
		string 		line;
		string 		prefix;

		while (getline(fileOut, line)) {
			size_t i;

			// trunk space
			for (i = 0; i < line.size() && (line[i] == ' ' || line[i] == '\t'); ++i)
				;
			if (i == (line.size() - 1))
				continue ;
			reportData << line.substr(i) << endl;
		}
	} else {
		string err = (string)"failed to open report file " + path;
		m_mgr.Log(LOGLEVEL_ERROR,err.c_str());
		m_mgr.ReportEventError(err.c_str());
	}
	m_mgr.ClientData(dataName.c_str(), reportData.str().c_str());
}


void	AgentStats::NetstatCentralized(const string & path) {
	stringstream		reportData;
	
	NetstatLocal(path, reportData);
	m_mgr.ClientData("netstat", reportData.str().c_str());
	NetstatCentralizedStep("netstat -na", path, "ports");
	NetstatCentralizedStep("netstat -nr", path, "route");
	NetstatCentralizedStep("ipconfig /all", path, "ipconfig");
	stringstream		reportDataStat;
	IfStat(reportDataStat);
	m_mgr.ClientData("ifstat", reportDataStat.str().c_str());
}


void AgentStats::Run() {
	string		path;
	TCHAR		buf[TEMP_PATH_LEN + 1];

	m_mgr.GetEnvironmentVariable("TEMP", buf, TEMP_PATH_LEN);
	path = buf;
	path += "\\netstat.tmp";
	if (m_mgr.IsCentralModeEnabled() == false) {
		stringstream reportData;

		ptime now = second_clock::local_time();
		date today = now.date();
		reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "]";
		reportData << endl;
		reportData << "win32" << endl;	
		NetstatLocal(path, reportData);
		m_mgr.Status(m_testName.c_str(), "green", reportData.str().c_str());
	} else {
		NetstatCentralized(path);
	}
}

bool AgentStats::Init() {
	if (m_mgr.IsCentralModeEnabled() == false) {
		PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

		if (conf == NULL)
			return false;
		PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string name, value;

			name = m_mgr.GetConfigurationRangeValue(range, "name");
			value = m_mgr.GetConfigurationRangeValue(range, "value");
			if (name == "testname") {
				m_testName = value;
			}
		}	
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	return true;
}

AgentStats::AgentStats(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_testName = "netstat";
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentStats(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &statsAgentInfo;
}
