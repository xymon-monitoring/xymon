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

#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
using namespace std;

#define BBWIN_AGENT_EXPORTS

#include "sample.h"

static const BBWinAgentInfo_t 		sampleAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"sample",    					// agentName;
	"this is a sample bbwin agent for example purpose",        // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE			// flags
};                

void 		AgentSample::Run() {
	stringstream 	reportData;	
        
	reportData << "sample monitoring agent\n" << endl;
    reportData << m_message << endl;
	if (m_mgr.IsCentralModeEnabled())
		m_mgr.ClientData(m_testName.c_str(), reportData.str().c_str());
	else
		m_mgr.Status(m_testName.c_str(), "green", reportData.str().c_str());
}

AgentSample::AgentSample(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_testName = "sample";
	m_message = "this is a simple test";
}

bool		AgentSample::Init() {
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
		if (name == "message") {
			m_message = value;
		}
	}	
	m_mgr.FreeConfigurationRange(range);
	m_mgr.FreeConfiguration(conf);
	return true;
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentSample(mgr);
}

BBWIN_AGENTDECL void DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &sampleAgentInfo;
}
