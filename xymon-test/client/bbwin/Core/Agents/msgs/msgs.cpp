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
// $Id: msgs.cpp 79 2008-02-08 15:03:16Z sharpyy $

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"
#include "msgs.h"
#include "Utils.h"

using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;
using namespace std;
using namespace utils;

static const BBWinAgentInfo_t 		msgsAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"msgs",								// agentName;
	"msgs agent : check the event logs",        // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE	// flags
};                

//
// common bb colors
//
static const char	*bbcolors[] = {"green", "yellow", "red", NULL};

//
// AgentMsgs methods
//
//

bool	AgentMsgs::InitCentralMode() {
	string clientLocalCfgPath = m_mgr.GetSetting("tmppath") + (string)"\\clientlocal.cfg";

	m_mgr.Log(LOGLEVEL_DEBUG, "start %s", __FUNCTION__);
	m_mgr.Log(LOGLEVEL_DEBUG, "checking file %s", clientLocalCfgPath.c_str());
	ifstream		conf(clientLocalCfgPath.c_str());

	if (!conf) {
		string	err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_INFO, "can't open %s : %s", clientLocalCfgPath.c_str(), err.c_str());
		return false;
	}
	m_mgr.Log(LOGLEVEL_DEBUG, "reading file %s", clientLocalCfgPath.c_str());
	string		buf, eventlog, ignore, trigger;
	bool			skipNextLineFlag = false;
	while (!conf.eof()) {
		string			value;

		if (skipNextLineFlag == true) {
			skipNextLineFlag = false;
		} else {
			utils::GetConfigLine(conf, buf);
		}
		if (utils::parseStrGetNext(buf, "eventlog:", value)) {
			string			sizeStr;

			if (utils::parseStrGetLast(value, ":", sizeStr)) {
				DWORD		maxData = 10240;
				value.erase(value.end() - (sizeStr.size() + 1), value.end());
				std::istringstream iss(sizeStr);
				iss >> maxData;
				if (maxData > 0)
					m_eventlog.SetMaxData(value, maxData);
				m_mgr.Log(LOGLEVEL_DEBUG, "will use maxdata size : %u", maxData);
			}
			// read next arguments : ignore and trigger options
			while (!conf.eof()) {
				string			arg;
				EventLog::Rule	rule;

				utils::GetConfigLine(conf, buf);
				if (utils::parseStrGetNext(buf, "ignore ", arg)) {
					rule.SetIgnore(true);
					rule.SetValue(arg);
					m_mgr.Log(LOGLEVEL_DEBUG, "will ignore : %s", arg.c_str());
					m_eventlog.AddRule(value, rule);
				} else if (utils::parseStrGetNext(buf, "trigger ", arg)) {
					rule.SetValue(arg);
					m_mgr.Log(LOGLEVEL_DEBUG, "will trigger : %s", arg.c_str());
					m_eventlog.AddRule(value, rule);
				} else {
					skipNextLineFlag = true;
					break ;
				}
			}
		}  
	}
	return false;
}

//
// Run method
// called from init
//
void AgentMsgs::Run() {
	stringstream 					reportData;	
	DWORD							finalState;
	
	if (m_mgr.IsCentralModeEnabled() == false) {
		ptime now = second_clock::local_time();
		finalState = GREEN;
		reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "] ";
		reportData << endl;
		finalState = m_eventlog.Execute(reportData);
		if (m_alwaysgreen)
			finalState = GREEN;

		reportData << "\nSummary:\n" << endl;
		reportData << format("- Events Analyzed: %6u") % m_eventlog.GetTotalCount() << endl ;
		reportData << format("- Events Matched:  %6u") % m_eventlog.GetMatchCount() << endl ;
		reportData << format("- Events Ignored:  %6u") % m_eventlog.GetIgnoreCount() << endl ;
		reportData << endl;
		m_mgr.Status(m_testName.c_str(), bbcolors[finalState], reportData.str().c_str());
	} else {
		m_eventlog.SetCentralized(true);
		InitCentralMode();
		m_eventlog.Execute(reportData);
		m_mgr.ClientData("", reportData.str().c_str());
	}
}

void	AgentMsgs::AddEventLogRule(PBBWINCONFIGRANGE range, bool ignore, const string defLogFile = "") {
	string		logfile, source, delay, type, alarmcolor, value, eventid;
	string		count, priority, user;
	EventLog::Rule		rule;
	
	if (defLogFile == "")
		logfile = m_mgr.GetConfigurationRangeValue(range, "logfile");
	else
		logfile = defLogFile;
	if (logfile.length() == 0)
		return ;
	alarmcolor = m_mgr.GetConfigurationRangeValue(range, "alarmcolor");
	source = m_mgr.GetConfigurationRangeValue(range, "source");
	value = m_mgr.GetConfigurationRangeValue(range, "value");
	delay = m_mgr.GetConfigurationRangeValue(range, "delay");
	type = m_mgr.GetConfigurationRangeValue(range, "type");
	eventid = m_mgr.GetConfigurationRangeValue(range, "eventid");
	count = m_mgr.GetConfigurationRangeValue(range, "count");
	priority = m_mgr.GetConfigurationRangeValue(range, "priority");
	user = m_mgr.GetConfigurationRangeValue(range, "user");
	if (value.length() > 0)
		rule.SetValue(value);
	if (alarmcolor.length() > 0) {
		if (alarmcolor == "green") 
			rule.SetAlarmColor(GREEN);
		else if (alarmcolor == "yellow")
			rule.SetAlarmColor(YELLOW);
		else if (alarmcolor == "red")
			rule.SetAlarmColor(RED);
	}
	if (source.length() > 0)
		rule.SetSource(source);
	rule.SetDelay(m_delay);
	if (delay.length() > 0) {
		DWORD del = m_mgr.GetSeconds(delay.c_str());
		if (del >= 60)
			rule.SetDelay(del);
	}
	if (eventid.length() > 0)
		rule.SetEventId(m_mgr.GetNbr(eventid.c_str()));
	if (priority.length() > 0)
		rule.SetPriority(m_mgr.GetNbr(priority.c_str()));
	if (count.length() > 0)
		rule.SetCount(m_mgr.GetNbr(count.c_str()));
	if (type.length() > 0)
		rule.SetType(type);
	if (user.length() > 0)
		rule.SetUser(user);
	rule.SetIgnore(ignore);
	m_eventlog.AddRule(logfile, rule);
}

//
// init function
//
bool AgentMsgs::Init() {
	m_mgr.Log(LOGLEVEL_DEBUG, "Begin Msgs Initialization %s", __FUNCTION__);
	if (m_mgr.IsCentralModeEnabled() == false) {
		PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

		if (conf == NULL)
			return false;
		PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string	name =  m_mgr.GetConfigurationRangeValue(range, "name");
			string  value = m_mgr.GetConfigurationRangeValue(range, "value");
			if (name == "alwaysgreen" && value == "true")
				m_alwaysgreen = true;
			if (name == "checkfulleventlog" && value == "true")
				m_eventlog.SetCheckFullLogFile(true);
			if (name == "testname" && value.length() > 0)
				m_testName = value;
			if (name == "delay" && value.length() > 0) {
				DWORD del = m_mgr.GetSeconds(value.c_str());
				if (del >= 60)
					m_delay = del;
			}
		}
		m_mgr.FreeConfigurationRange(range);
		range = m_mgr.GetConfigurationRange(conf, "match");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			AddEventLogRule(range, false);
		}
		m_mgr.FreeConfigurationRange(range);
		range = m_mgr.GetConfigurationRange(conf, "ignore");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			AddEventLogRule(range, true);
		}
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	m_mgr.Log(LOGLEVEL_DEBUG,"Ending Msgs Initialization");
	return true;
}


//
// constructor 
//
AgentMsgs::AgentMsgs(IBBWinAgentManager & mgr) : 
		m_mgr(mgr),
		m_delay(30 * 60),
		m_alwaysgreen(false),
		m_testName("msgs")
{

}


//
// Destructor 
//
AgentMsgs::~AgentMsgs() {
	
}


//
// common agents export functions
//

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentMsgs(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &msgsAgentInfo;
}
