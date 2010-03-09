//this file is part of BBWin
//Copyright (C)2006-2008 Etienne GRIGNON  ( etienne.grignon@gmail.com )
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
// $Id: BBWin.cpp 96 2008-05-21 17:30:49Z sharpyy $

#include <windows.h>
#include <sddl.h>
#include <process.h>
#include <tchar.h>
#include <iostream>
#include <assert.h>

#include <map>
#include <vector>
#include <string>
#include <fstream>
using namespace std;

#include "ou_thread.h"
using namespace openutils;

#include "Logging.h"
#include "BBWinConfig.h"
#include "BBWinService.h"
#include "BBWin.h"
#include "BBWinAgentManager.h"
#include "IBBWinAgent.h"
#include "BBWinHandler.h"
#include "BBWinMessages.h"

#include "Utils.h"
using namespace utils;

// this event is signalled when the
// service should end
//
HANDLE  hServiceStopEvent = NULL;
BBWin	* bbw = NULL;


//
//  FUNCTION: myReportStopPendingToSCMgr
//
//  PURPOSE: report the stop pending status to the SCS Manager
//
//  PARAMETERS:
//
//  RETURN VALUE:
//    none
//
static void		myReportStopPendingToSCMgr() {
	ReportStatusToSCMgr(SERVICE_STOP_PENDING, NO_ERROR, 3000);
}


//
//  FUNCTION: ServiceStart
//
//  PURPOSE: Actual code of the service that does the work.
//
//  PARAMETERS:
//    dwArgc   - number of command line arguments
//    lpszArgv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
VOID ServiceStart (DWORD dwArgc, LPTSTR *lpszArgv) {
	// report the status to the service control manager.
	//
	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000)) 
      return ;

	// create the event object. The control handler function signals
	// this event when it receives the "stop" control code.
	//
	hServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if ( hServiceStopEvent == NULL)
      return ;
	// report the status to the service control manager.
	//
	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000))
	{
		CloseHandle(hServiceStopEvent);
		return ;
	}

	bbw = BBWin::getInstancePtr();

	if (!ReportStatusToSCMgr(SERVICE_START_PENDING, NO_ERROR, 3000))
	{
		CloseHandle(hServiceStopEvent);
		return ;
	}
	if (!ReportStatusToSCMgr(SERVICE_RUNNING, NO_ERROR, 0))
    {
		CloseHandle(hServiceStopEvent);
		return ;
	}
	//
	// End of initialization
	//
	
	try {
		bbw->Start(hServiceStopEvent);
	} catch (BBWinException ex) {
		cout << "can't start : " << ex.getMessage();
		CloseHandle(hServiceStopEvent);
		hServiceStopEvent = NULL;
	}

	myReportStopPendingToSCMgr();
	bbw->Stop(myReportStopPendingToSCMgr);
	BBWin::freeInstance();
	
	if (hServiceStopEvent)
		CloseHandle(hServiceStopEvent);
}



//
//  FUNCTION: ServiceStop
//
//  PURPOSE: Stops the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//    If a ServiceStop procedure is going to
//    take longer than 3 seconds to execute,
//    it should spawn a thread to execute the
//    stop code, and return.  Otherwise, the
//    ServiceControlManager will believe that
//    the service has stopped responding.
//
VOID ServiceStop() {
	if ( hServiceStopEvent )
		SetEvent(hServiceStopEvent);
	
}


//
//  FUNCTION: BBWin
//
//  PURPOSE: BBWin class constructor 
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// Instantiate the logging class and set the default log path file
//
BBWin::BBWin() : 
	m_hCount(0),
	m_autoReload(false),
	m_autoReloadInterval(BBWIN_AUTORELOAD_INTERVAL),
	m_do_stop(false)
{
	m_log = Logging::getInstancePtr();
	m_conf = BBWinConfig::getInstancePtr();
	m_log->setFileName(BBWIN_LOG_DEFAULT_PATH);
	
	ZeroMemory(&m_hEvents, sizeof(m_hEvents));

	assert(m_log != NULL);
	assert(m_conf != NULL);

	// set default setting known by BBWin
	m_defaultSettings[ "bbdisplay" ] = &BBWin::callback_AddBBDisplay;
	m_defaultSettings[ "bbpager" ] = &BBWin::callback_AddBBPager;
	m_defaultSettings[ "usepager" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "timer" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "loglevel" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "logpath" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "logreportfailure" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "pagerlevels" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "autoreload"] = &BBWin::callback_SetAutoReload;
	m_defaultSettings[ "hostname" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "proxy" ] = &BBWin::callback_AddSimpleSetting;
	m_defaultSettings[ "useproxy" ] = &BBWin::callback_AddSimpleSetting;

	m_defaultSettings[ "mode" ] = &BBWin::callback_SetBBWinMode;
	m_defaultSettings[ "configclass" ] = &BBWin::callback_AddSimpleSetting;

	// call to the settings initialization
	InitSettings();
}


//
//  FUNCTION: ~BBWin
//
//  PURPOSE: BBWin class destructor 
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// Free the logging class instance
//
BBWin::~BBWin() {
	BBWinConfig::freeInstance();
	Logging::freeInstance();
}


//
//  FUNCTION: BBWin::InitSettings
//
//  PURPOSE: init the general settings for BBWin
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  
//
void			BBWin::InitSettings() {
	SetAutoReload(true);
	m_centralClient = NULL;
	m_centralMode = false;
	ZeroMemory(&m_confTime, sizeof(m_confTime));
	m_setting[ "timer" ] = "300";
	m_setting[ "loglevel" ] = "0";
	m_setting[ "logpath" ] = BBWIN_LOG_DEFAULT_PATH;
	m_setting[ "bbwinconfigfilename" ] = BBWIN_CONFIG_FILENAME;
	m_setting[ "pagerlevels" ] = "red purple";
	m_setting[ "hostname" ] = "%COMPUTERNAME%";
	m_setting[ "configclass" ] = "win32";
	m_setting[ "usepager" ] = "false";
	m_setting[ "useproxy" ] = "false";
}


//  FUNCTION: BBWin::callback_SetAutoReload
//
//  PURPOSE: enable or disable the autoreload function (callback)
//
//  PARAMETERS:
//    call back parameters
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  
//
void			BBWin::callback_SetAutoReload(const std::string & name, const std::string & value) {
	if (value == "true") {
		SetAutoReload(true);
	} else if (value == "false") {
		SetAutoReload(false);
	}
}

//
//  FUNCTION: BBWin::SetAutoReload
//
//  PURPOSE: enable or disable the autoreload function
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  
//
void			BBWin::SetAutoReload(bool value) {
	// enable autoreload
	if (value == true && m_autoReload == false) {
		m_autoReload = true;
	}
	// disable autoreload
	else if (value == false && m_autoReload == true) {
		m_autoReload = false;
	}
}

//  FUNCTION: BBWin::callback_SetBBWinMode
//
//  PURPOSE: set the mode of working for BBWin : local configuration or centralized (hobbit mode)
//
//  PARAMETERS:
//    call back parameters
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  
//
void			BBWin::callback_SetBBWinMode(const std::string & name, const std::string & value) {
	if (value == "central") {
		m_centralMode = true;
	} else if (value == "local") {
		m_centralMode = false;
	}
}


//
//  FUNCTION: BBWin::GetConfFileChanged
//
//  PURPOSE: check if the configuration file has changed
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    bool			if the file has changed, it return true
//
//  COMMENTS:
//  
//
bool			BBWin::GetConfFileChanged() {
	FILETIME	time;
	bool		res = false;

	HANDLE   file = CreateFile(m_setting["configpath"].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(file && GetFileTime(file, NULL, NULL, &time)) {
		if (m_confTime.dwHighDateTime == 0 && m_confTime.dwLowDateTime == 0) {
			m_confTime.dwHighDateTime = time.dwHighDateTime;
			m_confTime.dwLowDateTime = time.dwLowDateTime;
		} else {
			if (m_confTime.dwHighDateTime != time.dwHighDateTime || m_confTime.dwLowDateTime != time.dwLowDateTime) {
				m_confTime.dwHighDateTime = time.dwHighDateTime;
				m_confTime.dwLowDateTime = time.dwLowDateTime;
				res = true;
			}
		}
		if( file ) 
			CloseHandle( file ); 
	}
	return res;
}

//
//  FUNCTION: BBWin::BBWinRegQueryValueEx
//
//  PURPOSE: get a registry value
//
//  PARAMETERS:
//     hKey : handle to the registry
//     key : key name
//     dest : key value destination 
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// Read a registry value
//
void				BBWin::BBWinRegQueryValueEx(HKEY hKey, const string & key, string & dest) {
	unsigned long 	lDataType;
	unsigned long 	lBufferLength = BB_PATH_LEN;
	char	 		tmpPath[BB_PATH_LEN + 1];
	string			err;

	if (RegQueryValueEx(hKey, key.c_str(), NULL, &lDataType, (LPBYTE)tmpPath, &lBufferLength) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		err = key;
		err += " : can't open it";
		throw BBWinException(err.c_str());
	}
	if (lDataType != REG_SZ)
	{
		RegCloseKey(hKey);
		err = key;
		err += ": invalid type";
		throw BBWinException(err.c_str());
	}
	if (lBufferLength == BB_PATH_LEN)
		tmpPath[BB_PATH_LEN] = '\0';
	dest = tmpPath;
}

//
//  FUNCTION: BBWin::LoadRegistryConfiguration
//
//  PURPOSE: load the registry configuration
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// Read the BBWin registry configuration
//
void 				BBWin::LoadRegistryConfiguration() {
	HKEY 			hKey;
	string			tmp;

	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("Software\\BBWin"), 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
	{		
		LPCTSTR		arg[] = {"HKEY_LOCAL_MACHINE\\SOFTWARE\\BBWin : can't open it", NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_NO_REGISTRY, 1, arg);
		throw BBWinException("Can't open HKEY_LOCAL_MACHINE\\SOFTWARE\\BBWin");
	}
	try {
		BBWinRegQueryValueEx(hKey, "tmppath", tmp);
		m_setting["tmppath"] = tmp;
		tmp.clear();
	
	} catch (BBWinException ex) {
		LPCTSTR		arg[] = {"tmppath", NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_NO_REGISTRY, 1, arg);
		throw ex;
	}
	try {
		BBWinRegQueryValueEx(hKey, "binpath", tmp);
		m_setting["binpath"] = tmp;
		tmp.clear();
	} catch (BBWinException ex) {
		LPCTSTR		arg[] = {"binpath", NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_NO_REGISTRY, 1, arg);
		throw ex;
	}
	try {
		BBWinRegQueryValueEx(hKey, "etcpath", tmp);
		m_setting["etcpath"] = tmp;
		tmp.clear();
	} catch (BBWinException ex) {
		LPCTSTR		arg[] = {"etcpath", NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_NO_REGISTRY, 1, arg);
		throw ex;
	}
	try {
		BBWinRegQueryValueEx(hKey, "hostname", tmp);
		m_setting["hostname"] = tmp;
		tmp.clear();
	} catch (BBWinException ex) {
		LPCTSTR		arg[] = {"hostname", NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_NO_REGISTRY, 1, arg);
		throw ex;
	}
	RegCloseKey(hKey);
}

//
//  FUNCTION: BBWin::Init
//
//  PURPOSE: init the BBWin engine
//
//  PARAMETERS:
//    h  	service stop event handle
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// Init the BBWin engine by setting the handle table and 
// call the registry configuration
//
void			BBWin::Init(HANDLE h) {
	m_hEvents[0] = h;
	m_hCount++;
	try {
		LoadRegistryConfiguration();
	} catch (BBWinException ex) {
		throw ex;
	}
}

//
//  FUNCTION: BBWin::callback_AddSimpleSetting
//
//  PURPOSE: add a simple setting to the global setting map
//
//  PARAMETERS:
//    name 		setting name
//    value		value
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void			BBWin::callback_AddSimpleSetting(const std::string & name, const std::string & value) {
	m_setting[name] = value;
}

//
//  FUNCTION: BBWin::callback_AddBBDisplay
//
//  PURPOSE:  add a BBDisplay to the BBDisplay vector
//
//  PARAMETERS:
//    name 		setting name
//    value		value
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
//
void			BBWin::callback_AddBBDisplay(const string & name, const string & value) {
	m_bbdisplay.push_back(value);
}


//
//  FUNCTION: BBWin::callback_AddBBPager
//
//  PURPOSE:  add a BBPager to the BBPagersvector
//
//  PARAMETERS:
//    name 		setting name
//    value		value
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
//
void			BBWin::callback_AddBBPager(const string & name, const string & value) {
	m_bbpager.push_back(value);
}


//
//  FUNCTION: BBWin::LoadConfiguration
//
//  PURPOSE: load the configuration from file
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// Entry point to Load the configuration file
//
void							BBWin::LoadConfiguration() {
	string						tmp;

	tmp = m_setting["etcpath"];
	tmp += "\\";
	tmp += BBWIN_CONFIG_FILENAME;
	m_setting["configpath"] = tmp;
	m_configuration.clear();
	try {	
		m_conf->LoadConfiguration(m_setting["configpath"], BBWIN_NAMESPACE_CONFIG, m_configuration);
	} catch (BBWinConfigException ex) {
		string		err = ex.getMessage();
		LPCTSTR		arg[] = {m_setting["configpath"].c_str(), err.c_str(), NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_NO_CONFIGURATION, 2, arg);
		throw ex;
	}
}

//
//  FUNCTION: BBWin::CheckConfiguration
//
//  PURPOSE: check the configuration loaded
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// Check if all necessary settings are present
//
void				BBWin::CheckConfiguration() {
	std::pair< config_iter_t, config_iter_t > range;
	range = m_configuration.equal_range("setting");
	for ( ; range.first != range.second; ++range.first) {
		map< string, load_simple_setting >::const_iterator i = m_defaultSettings.find( range.first->second["name"] );
		if ( i == m_defaultSettings.end() ) {
			LPCTSTR		arg[] = {BBWIN_CONFIG_FILENAME, BBWIN_NAMESPACE_CONFIG, "setting", range.first->second["name"].c_str(), NULL};
			m_log->reportWarnEvent(BBWIN_SERVICE, EVENT_INVALID_CONFIGURATION, 4, arg);
		} else {
			// experimenting pointer to member functions :) some people say "it's nice", some people say "it's ugly"
			(this->*(i->second))(range.first->second["name"], range.first->second["value"]);
		}
	}
	if (m_bbdisplay.size() == 0) {
		LPCTSTR		arg[] = {m_setting["configpath"].c_str(), NULL};
		m_log->reportWarnEvent(BBWIN_SERVICE, EVENT_NO_BB_DISPLAY, 1, arg);
	}
	ReplaceEnvironmentVariableStr(m_setting["hostname"]);
	ReplaceEnvironmentVariableStr(m_setting["logpath"]);
	ReplaceEnvironmentVariableStr(m_setting["binpath"]);
	ReplaceEnvironmentVariableStr(m_setting["etcpath"]);
	ReplaceEnvironmentVariableStr(m_setting["tmppath"]);
	if (m_setting["hostname"].size() == 0) {
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_NO_HOSTNAME, 0, NULL);
		throw BBWinConfigException("no hostname configured");
	}
	if (m_setting[ "logpath" ].size() != 0) {
		m_log->setFileName(m_setting[ "logpath" ]);
	}
	if (m_setting[ "loglevel" ].size() != 0 ) {
		m_log->setLogLevel(GetNbr(m_setting[ "loglevel" ]));
	}
	if (SetCurrentDirectory(m_setting[ "binpath" ].c_str()) == 0) {
		LPCTSTR		arg[] = {m_setting[ "binpath" ].c_str(), NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_BAD_PATH, 1, arg);
		throw BBWinConfigException("bad BBWin bin path");
	}
}

//
//  FUNCTION: BBWin::StartAgents
//
//  PURPOSE: Starts the agent with their own threads if they need it
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void			BBWin::StartAgents() {
	map<string, BBWinHandler * >::iterator	itr;
	
	// first start local agents
	for (itr = m_agents.begin(); itr != m_agents.end(); ++itr) {
		BBWinHandler		& handler = *((*itr).second);
		BBWinLocalHandler	* localHandler = NULL;

		try {
			localHandler = new BBWinLocalHandler(handler);
		} catch (std::bad_alloc ex) {
			continue ;
		}
		m_localHandlers.insert(pair< std::string, BBWinLocalHandler * >(handler.GetAgentName(), localHandler));
		localHandler->start();
	}
	// second start 
	if (m_centralMode) {
		m_log->log(LOGLEVEL_DEBUG, "Starting hobbit client agent.");
		m_centralClient->start();
	}
}


//
//  FUNCTION: BBWin::StopAgents
//
//  PURPOSE: Stop the agents threads
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void			BBWin::StopAgents() {
	map<string, BBWinLocalHandler * >::iterator itr;

	// first stop the agents with local configuration which have their own threads
	m_log->log(LOGLEVEL_DEBUG, "Listing Agents Threads to terminate.");
	for (itr = m_localHandlers.begin(); itr != m_localHandlers.end(); ++itr)
		;
	m_log->log(LOGLEVEL_DEBUG, "Agents Threads Terminating.");
	for (itr = m_localHandlers.begin(); itr != m_localHandlers.end(); ++itr) {
		if (m_do_stop == true && m_report_stop != NULL) {
			m_report_stop();
		}
		(*itr).second->stop();
	}
	m_log->log(LOGLEVEL_DEBUG, "Agents Threads Terminated.");
	for (itr = m_localHandlers.begin(); itr != m_localHandlers.end(); ++itr) {
		delete (*itr).second;
	}
	m_log->log(LOGLEVEL_DEBUG, "Agents Threads Deleted.");
	m_localHandlers.clear();
	m_log->log(LOGLEVEL_DEBUG, "Agents Threads Table Clear Done.");
	// second : we stop the thread for the centralized agent part
	if (m_centralMode) {
		m_log->log(LOGLEVEL_DEBUG, "Stopping hobbit client agent.");
		m_centralClient->stop();
		delete m_centralClient;
	}
}


//
//  FUNCTION: BBWin::LoadAgents
//
//  PURPOSE: load the agents
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  very important function. It starts the agent threads.
// agent timer can be personnalized for each agent by setting the timer attribute
// timer minimum is 5 seconds
// timer maximum is 31 days
//
void				BBWin::LoadAgents() {
	std::pair< config_iter_t, config_iter_t > range;
	BBWinHandler 			*hand;
	
	//
	// If the central mode is created then, the environment for the 
	// central mode is created
	//
	if (m_centralMode) {
		try {
			string title = "centralclient";
			bbwinhandler_data_t		data = {m_hEvents, 
										m_hCount, 
										title, 
										title, 
										m_bbdisplay, 
										m_bbpager, 
										m_setting, 
										GetSeconds(m_setting[ "timer" ])};
			m_centralClient = new BBWinCentralHandler(data);
		} catch (std::bad_alloc ex) {
			m_log->log(LOGLEVEL_ERROR, "no more memory");
			m_centralMode = false;
		}
	}
	range = m_configuration.equal_range("load");
	for ( ; range.first != range.second; ++range.first) {
		DWORD		timer = GetSeconds(m_setting[ "timer" ]);

		if (range.first->second["timer"].size() != 0) {
			timer = GetSeconds(range.first->second["timer"]);
			if (timer < 5 || timer > 2678400) { // invalid time
				LPCTSTR		arg[] = {range.first->second["name"].c_str(), m_setting[ "timer" ].c_str(), NULL};
				m_log->reportWarnEvent(BBWIN_SERVICE, EVENT_INVALID_TIMER, 2, arg);
				timer = GetNbr(m_setting[ "timer" ]);
			}
		}
		bbwinhandler_data_t		data = {m_hEvents, 
										m_hCount, 
										range.first->second["name"], 
										range.first->second["value"], 
										m_bbdisplay, 
										m_bbpager, 
										m_setting, 
										timer};
		
		map< std::string, BBWinHandler * >::iterator itr;
		
		itr = m_agents.find(range.first->second["name"]);
		if (itr == m_agents.end()) {
			try {
				hand = new BBWinHandler(data);
			} catch (std::bad_alloc ex) {
				m_log->log(LOGLEVEL_ERROR, "no more memory");
				continue ;
			}
			//
			// We decide here if the agent will work in the centralized or local mode
			//
			//
			if (m_centralMode && hand->GetAgentFlags() & BBWIN_AGENT_CENTRALIZED_COMPATIBLE) {
				m_centralClient->AddAgentHandler(hand);
			} else {
				hand->Init();
				m_agents.insert(pair< std::string, BBWinHandler * >(range.first->second["name"], hand));
			}
		} else {
			LPCTSTR		arg[] = {range.first->second["name"].c_str(), NULL};
			m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_ALREADY_LOADED_AGENT, 1, arg);
		}
	}
}

//
//  FUNCTION: BBWin::UnloadAgents
//
//  PURPOSE: Unload the agents
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  just delete the BBWinHandlers objects 
//
void				BBWin::UnloadAgents() {
	map<string, BBWinHandler * >::iterator	itr;
	
	m_log->log(LOGLEVEL_DEBUG, "Listing Agents to unload.");
	for (itr = m_agents.begin(); itr != m_agents.end(); ++itr) {
		delete (*itr).second;
	}
	m_log->log(LOGLEVEL_DEBUG, "Agents deleted.");
	m_agents.clear();
}


//
//  FUNCTION: BBWin::WaitFor
//
//  PURPOSE: Main loop : wait for service stopping  event
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void						BBWin::WaitFor() {
	DWORD					dwWait;
	DWORD					waiting;

	for (;;) {
		waiting = INFINITE;
		if (m_autoReload)
			waiting = m_autoReloadInterval * 1000;
		dwWait = WaitForMultipleObjects( 1, m_hEvents, FALSE, waiting);
		if ( dwWait == WAIT_OBJECT_0 )    // service stop signaled
			break;           
		if (dwWait == WAIT_ABANDONED_0)
			break ;
		if (m_autoReload) {
			if (GetConfFileChanged()) {
				try {
					Reload();
				} catch (BBWinException ex) {
					throw ex;
				}
			}
		}
	}
}


//
//  FUNCTION: BBWin::_Start
//
//  PURPOSE: Private starting function
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void		BBWin::_Start() {
	bool	confLoaded = true;

	try {
		LoadRegistryConfiguration();
		LoadConfiguration();
		CheckConfiguration();
	} catch (BBWinConfigException _ex) {
		// no fatal error
		// we do not exit the program anymore
		confLoaded = false;
	}
	if (confLoaded) {
		try {
			LoadAgents();
			StartAgents();
		} catch (BBWinConfigException _ex) {
			// no fatal error
			// we do not exit the program anymore
		}
	}
	m_log->log(LOGLEVEL_INFO, "bbwin is started.");
	LPCTSTR		argStart[] = {SZSERVICENAME, m_setting["hostname"].c_str(), NULL};
	m_log->reportInfoEvent(BBWIN_SERVICE, EVENT_SERVICE_STARTED, 2, argStart);
}


//
//  FUNCTION: BBWin::Reload
//
//  PURPOSE: Stop the agents, reload the BBWin configuration and restart the agents
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void		BBWin::Reload() {
	m_log->log(LOGLEVEL_INFO, "bbwin is reloading the configuration.");
	m_log->reportInfoEvent(BBWIN_SERVICE, EVENT_SERVICE_RELOAD, 0, NULL);
	SetEvent(m_hEvents[BBWIN_STOP_HANDLE]);
	StopAgents();
	UnloadAgents();
	m_setting.clear();
	m_bbdisplay.clear();
	m_bbpager.clear();
	ResetEvent(m_hEvents[BBWIN_STOP_HANDLE]);
	InitSettings();
	_Start();
}


//
//  FUNCTION: BBWin::Start
//
//  PURPOSE: Run method 
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// public entry point to run the BBWin engine
//
void 			BBWin::Start(HANDLE h) {
	try {
		Init(h);
	} catch (BBWinException ex) {
		throw ex;
	}
	_Start();
	try {
		WaitFor();
	} catch (BBWinException ex) {
		throw ex;
	}
}

//
//  FUNCTION: BBWin::Stop
//
//  PURPOSE: Stop the BBWin
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// public entry point to stop the BBWin engine
//
void 			BBWin::Stop(void (*report_stop)(void)) {
	m_do_stop = true;
	m_report_stop = report_stop;
	StopAgents();
	UnloadAgents();
	m_log->log(LOGLEVEL_INFO, "bbwin is stopped.");
	LPCTSTR		argStop[] = {SZSERVICENAME, NULL};
	m_log->reportInfoEvent(BBWIN_SERVICE, EVENT_SERVICE_STOPPED, 1, argStop);
}

// BBWinException
BBWinException::BBWinException(const char* m) {
	msg = m;
}

string BBWinException::getMessage() const {
	return msg;
}
