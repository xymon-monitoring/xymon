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
// $Id: BBWinCentralHandler.cpp 96 2008-05-21 17:30:49Z sharpyy $

#include <windows.h>
#include <process.h>
#include <assert.h>
#include <time.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <sys/types.h>
#include <sys/timeb.h>
#include "ou_thread.h"
#include "Logging.h"
#include "BBWinNet.h"
#include "BBWinHandler.h"
#include "BBWinCentralHandler.h"
#include "BBWinMessages.h"
#include "BBWinService.h"
#include "Utils.h"
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"

using namespace openutils;
using namespace utils;
using namespace std;
using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;

//
// globals
//
static std::string						reportPath;
static std::string						reportSavePath;
static std::string						clientLocalCfgPath;

BBWinCentralHandler::BBWinCentralHandler(bbwinhandler_data_t & data) :
						m_data (data),
						m_timer (data.timer),
						m_hEvents (data.hEvents),
						m_hCount (data.hCount),
						m_bbdisplay (data.bbdisplay)
{
	std::ostringstream pid;

	pid << _getpid();
	m_log = Logging::getInstancePtr();
	reportSavePath = data.setting["tmppath"] + (string)"\\msg." + data.setting["hostname"] + (string)".txt";
	reportPath = reportSavePath + (string)"." + pid.str();
}

BBWinCentralHandler::~BBWinCentralHandler() {
	std::list<BBWinHandler *>::iterator		itr;

	for (itr = m_agents.begin(); itr != m_agents.end(); ++itr) {
		delete (*itr);
	}
}

void	BBWinCentralHandler::GetClock() {
	char timebuf[26], ampm[] = "AM";
	time_t					ltime;
	struct tm				today, gmt;
	errno_t					err;
	stringstream			report;

	_tzset();
	// Get UNIX-style time and display as number and string. 
	time( &ltime );
	report << boost::format("epoch: %ld") % (long int)ltime << endl;
	err = _localtime64_s( &today, &ltime );
	if (err) {
		string	errmesg;

		GetLastErrorString(errmesg);
		m_log->log(LOGLEVEL_ERROR, "failed _localtime64_s : %s", errmesg);
	   return ;
    }
	err = ctime_s(timebuf, 26, &ltime);
	if (err) {
		string	errmesg;

		GetLastErrorString(errmesg);
		m_log->log(LOGLEVEL_ERROR, "failed ctime_s : %s", errmesg);
       return ;
    }
	timebuf[24] = '\0';
	report << boost::format("local: %s") % timebuf << endl;
	bbwinClientData_callback("date", timebuf);
    // Display UTC. 
    err = _gmtime64_s( &gmt, &ltime );
    if (err) {
       string	errmesg;

		GetLastErrorString(errmesg);
		m_log->log(LOGLEVEL_ERROR, "failed _gmtime64_s : %s", errmesg);
	   return ;
    }
    err = asctime_s(timebuf, 26, &gmt);
    if (err) {
       string	errmesg;

		GetLastErrorString(errmesg);
		m_log->log(LOGLEVEL_ERROR, "failed asctime_s : %s", errmesg);
	   return ;
    }
	timebuf[24] = '\0';
    report << boost::format("UTC: %s") % timebuf << endl;
	bbwinClientData_callback("clock", report.str());
}

void BBWinCentralHandler::run() {
	DWORD 		dwWait;
	std::list<BBWinHandler *>::iterator		itr;
	
	clientLocalCfgPath = m_data.setting["tmppath"] + (string)"\\clientlocal.cfg";
	for (;;) {
		std::ofstream	report(reportPath.c_str(), std::ios_base::trunc | ios_base::binary);
		bool		created = false;

		if (report) {
			report << "client " << m_data.setting["hostname"] << ".bbwin " << m_data.setting["configclass"] << "\n";
			report.close();
			created = true;
			GetClock();
			string		osversion;
			uname(osversion);
			bbwinClientData_callback("osversion", osversion);
		} else {
			string mess, err;

			mess = (string)"failed to create the report file " + reportPath;
			GetLastErrorString(err);
			LPCTSTR		args[] = {mess.c_str(), err.c_str(), NULL};
			m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_HOBBIT_FAILED_CLIENTDATA, 2, args);
		}

		for (itr = m_agents.begin(); itr != m_agents.end(); ++itr) {
			(*itr)->Run();
			dwWait = WaitForMultipleObjects(m_hCount, m_hEvents , FALSE, 0);
			if (( dwWait >= WAIT_OBJECT_0 && dwWait <= (WAIT_OBJECT_0 + m_hCount - 1) ) || (dwWait >= WAIT_ABANDONED_0 && dwWait <= (WAIT_ABANDONED_0 + m_hCount - 1) )) {
				DeleteFile(reportPath.c_str());
				return ;
			}
		}
		if (created) {
			bbdisplay_t::iterator			itr;
			
			string		result;

			for (itr = m_bbdisplay.begin(); itr != m_bbdisplay.end(); ++itr) {
				BBWinNet	hobNet;
				hobNet.SetBBDisplay((*itr));
				try {
					hobNet.ClientData(reportPath, clientLocalCfgPath);
				} catch (BBWinNetException ex) {
					string mess, err;

					GetLastErrorString(err);
					mess = ex.getMessage();
					LPCTSTR		args[] = {mess.c_str(), err.c_str(), NULL};
					m_log->reportErrorEvent(BBWIN_SERVICE, EVENT_HOBBIT_FAILED_CLIENTDATA, 2, args);
				}
			}
			DeleteFile(reportSavePath.c_str());
			MoveFile(reportPath.c_str(), reportSavePath.c_str());
			DeleteFile(reportPath.c_str());
		}
		dwWait = WaitForMultipleObjects(m_hCount, m_hEvents , FALSE, m_timer * 1000 );
		if ( dwWait >= WAIT_OBJECT_0 && dwWait <= (WAIT_OBJECT_0 + m_hCount - 1) ) {    
			break ;
		} else if (dwWait >= WAIT_ABANDONED_0 && dwWait <= (WAIT_ABANDONED_0 + m_hCount - 1) ) {
			break ;
		}
	}
}

void			BBWinCentralHandler::bbwinClientData_callback(const std::string & dataName, const std::string & data) {
	std::ofstream	report(reportPath.c_str(), std::ios_base::app | ios_base::binary);
		
	if (report) {
		string		dataFormated;

		if (dataName.size() > 0)
			report << "[" << dataName << "]\n";
		dataFormated = data;
		// remove '\r; characters before sending
		dataFormated.erase(std::remove(dataFormated.begin(), dataFormated.end(), '\r'), dataFormated.end());
		report << dataFormated;
		if (dataFormated.size() > 1 && dataFormated.substr(dataFormated.size() - 1, 1) != "\n")
			report << "\n";
	}
}

void	BBWinCentralHandler::AddAgentHandler(BBWinHandler *handler) {
	assert(handler != NULL);
	
	handler->SetCentralMode(true);
	handler->Init();
	handler->SetClientDataCallBack(BBWinCentralHandler::bbwinClientData_callback);
	m_agents.push_back(handler);
}
