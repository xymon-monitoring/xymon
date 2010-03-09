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
// $Id: BBWinHandler.cpp 96 2008-05-21 17:30:49Z sharpyy $

#include <windows.h>
#include <assert.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include "ou_thread.h"
#include "BBWinNet.h"
#include "BBWinHandler.h"
#include "BBWinMessages.h"
#include "Utils.h"

using namespace std;
using namespace openutils;
using namespace utils;

BBWinHandler::BBWinHandler(bbwinhandler_data_t & data) : 
							m_bbdisplay (data.bbdisplay), 
							m_setting (data.setting),
							m_agentName (data.agentName),
							m_agentFileName (data.agentFileName),
							m_hEvents (data.hEvents),
							m_hCount (data.hCount),
							m_timer (data.timer),
							m_create (NULL),
							m_destroy (NULL),
							m_getinfo (NULL),
							m_initSucceed (false),
							m_loadSucceed (false)
{
	m_log = Logging::getInstancePtr();
	try {
		m_mgr = new BBWinAgentManager(data);
	} catch (std::bad_alloc ex) {
		throw ex;
	}
	if (m_mgr == NULL)
		throw std::bad_alloc("no more memory");
	try {
		init();
	} catch (BBWinHandlerException ex) {
		m_log->log(LOGLEVEL_DEBUG, "Init for agent failed.");
		return ;
	}
	m_loadSucceed = true;
	checkAgentCompatibility();
}

BBWinHandler::~BBWinHandler() {
	string		mess;

	if (m_loadSucceed) {
		mess = "Agent " + m_agentName + " going to be destroyed.";
		m_log->log(LOGLEVEL_DEBUG, mess.c_str());
		m_destroy(m_agent);
		mess = "Agent " + m_agentName + " destroyed.";
		m_log->log(LOGLEVEL_DEBUG, mess.c_str());
		FreeLibrary(m_hm);
		mess = "Agent DLL " + m_agentName + " Unloaded.";
		m_log->log(LOGLEVEL_DEBUG, mess.c_str());
		delete m_mgr;
		mess = "Thread for agent " + m_agentName + " stopped.";
		m_log->log(LOGLEVEL_DEBUG, mess.c_str());
	}
}



//
//  FUNCTION: BBWinHandler
//
//  PURPOSE: GetAgentFlags
//
//  PARAMETERS:
//    
//    
//
//  RETURN VALUE:
//    return the flags of the agent
//
//  COMMENTS:
//
//
DWORD				BBWinHandler::GetAgentFlags() {
	if (m_getinfo != NULL) {
		const BBWinAgentInfo_t 	* info = m_getinfo();
		return info->agentFlags;
	}
	return 0;
}

//
//  FUNCTION: BBWinHandler::init
//
//  PURPOSE: load the dll, get the adress procedure and instantiate the agent object
//
//  PARAMETERS:
//    none
//    
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  
//
void		BBWinHandler::init() {
	if ((m_hm = LoadLibrary(m_agentFileName.c_str())) == NULL) {
		string err;
		GetLastErrorString(err);
		LPCTSTR		arg[] = { m_agentFileName.c_str(), err.c_str(), NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_LOAD_AGENT_ERROR, 2, arg);
		throw BBWinHandlerException("can't load agent");
	} 
	if ((m_create = (CREATEBBWINAGENT)GetProcAddress(m_hm, "CreateBBWinAgent")) == NULL) {
		string err;
		GetLastErrorString(err);
		LPCTSTR		arg[] = { m_agentFileName.c_str(), err.c_str(), NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_LOAD_AGENT_ERROR, 2, arg);
		FreeLibrary(m_hm);
		throw BBWinHandlerException("can't get proc CreateBBWinAgent");
	}
	if ((m_destroy = (DESTROYBBWINAGENT)GetProcAddress(m_hm, "DestroyBBWinAgent")) == NULL) {
		string err;
		GetLastErrorString(err);
		LPCTSTR		arg[] = { m_agentFileName.c_str(), err.c_str(), NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_LOAD_AGENT_ERROR, 2, arg);
		FreeLibrary(m_hm);
		throw BBWinHandlerException("can't get proc DestroyBBWinAgent");
	}
	if ((m_getinfo = (GETBBWINAGENTINFO)GetProcAddress(m_hm, "GetBBWinAgentInfo")) == NULL) {
		string err;
		GetLastErrorString(err);
		LPCTSTR		arg[] = { m_agentFileName.c_str(), err.c_str(), NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_LOAD_AGENT_ERROR, 2, arg);
		FreeLibrary(m_hm);
		throw BBWinHandlerException("can't get proc GetBBWinAgentInfo");
	}
	m_agent = m_create(*m_mgr);
	if (m_agent == NULL) {
		string err;
		GetLastErrorString(err);
		LPCTSTR		arg[] = { m_agentFileName.c_str(), err.c_str(), NULL};
		m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_CANT_INSTANTIATE_AGENT, 2, arg);
		FreeLibrary(m_hm);
		throw BBWinHandlerException("can't allocate agent");
	}
}

//
//  FUNCTION: BBWinHandler::checkAgentCompatiblity
//
//  PURPOSE: it reports agent loading success for the moment
//
//  PARAMETERS:
//    none
//    
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//  report loading success
// in future, this function will also check agent compatibility
//
void		BBWinHandler::checkAgentCompatibility() {
	const BBWinAgentInfo_t 	* info = m_getinfo();
	ostringstream 			oss;
	
	if (info == NULL) {
		// error
	}
	if (info->bbwinVersion != BBWIN_AGENT_VERSION) {
		// for future use
	}
	oss << ". Agent name : " << info->agentName;
	oss << ". Agent description : " << info->agentDescription;
	string result = oss.str();
	LPCTSTR		arg[] = {m_agentFileName.c_str(), result.c_str(), NULL};
	m_log->reportInfoEvent(BBWIN_SERVICE, EVENT_LOAD_AGENT_SUCCESS, 2, arg);
}

//
//  FUNCTION: BBWinHandler::Init
//
//  PURPOSE: Call init agent function
//
//  PARAMETERS:
//    none
//    
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void BBWinHandler::Init() {
	if (m_loadSucceed)
		m_initSucceed = m_agent->Init();
}

//
//  FUNCTION: BBWinHandler::run
//
//  PURPOSE: running part
//
//  PARAMETERS:
//    none
//    
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void BBWinHandler::Run() {
	if (m_initSucceed) {
		if (m_mgr->IsCentralModeEnabled()) { // central configuration
			m_agent->Run();
		} else { // local configuration
			for (;;) {
				DWORD 		dwWait;

				m_agent->Run();
				dwWait = WaitForMultipleObjects(m_hCount, m_hEvents , FALSE, m_timer * 1000 );
				if ( dwWait >= WAIT_OBJECT_0 && dwWait <= (WAIT_OBJECT_0 + m_hCount - 1) ) {    
					break ;
				} else if (dwWait >= WAIT_ABANDONED_0 && dwWait <= (WAIT_ABANDONED_0 + m_hCount - 1) ) {
					break ;
				}
			}
		}
	}
}

//
//  FUNCTION: BBWinHandler::SetCentralMode
//
//  PURPOSE: set the BBWin mode
//
//  PARAMETERS:
//    mode		central mode if true
//    
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void				BBWinHandler::SetCentralMode(bool mode) {
	assert(m_mgr != NULL);
	m_mgr->SetCentralMode(mode);
}


//
//  FUNCTION: BBWinHandler::SetClientDataCallBack
//
//  PURPOSE: set the client data callback for the Agent Manager
//
//  PARAMETERS:
//    none
//    
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void				BBWinHandler::SetClientDataCallBack(clientdata_callback_t callback) {
	assert(m_mgr != NULL);
	m_mgr->SetCallBackClientData(callback);
}


// BBWinHandlerException
BBWinHandlerException::BBWinHandlerException(const char* m) {
	msg = m;
}

string BBWinHandlerException::getMessage() const {
	return msg;
}
