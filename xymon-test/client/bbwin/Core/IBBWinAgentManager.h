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
// $Id: IBBWinAgentManager.h 96 2008-05-21 17:30:49Z sharpyy $

#ifndef 	__IBBWINAGENTMANAGER_H__
#define		__IBBWINAGENTMANAGER_H__

#include <string>
#include <map>

#include "IBBWinException.h"


//
// BBWin Configuration types
//
struct BBWINCONFIG;
struct BBWINCONFIGRANGE;
typedef BBWINCONFIG *		PBBWINCONFIG;
typedef BBWINCONFIGRANGE *		PBBWINCONFIGRANGE;


//
// BBWinAgentManager is from the simple design pattern facade 
// to be able to the agent to use bbwin callback
// this will allow agents to be written without re coding every thing
//
class   IBBWinAgentManager {
	public :
		// return the agent name
		virtual LPCTSTR			GetAgentName() = 0;
		// return agent file name (path)
		virtual LPCTSTR			GetAgentFileName() = 0;
		// return the agent timer setting
		virtual DWORD				GetAgentTimer() = 0;
		// get an environnement variable (copy the string in dest, size argument is size of dest)
		virtual void				GetEnvironmentVariable(LPCTSTR varname, LPTSTR dest, DWORD size) = 0;
		// get the last error in dest, size argument is size of dest
		virtual void			GetLastErrorString(LPTSTR dest, DWORD size) = 0; 
		// return a converted number
		virtual DWORD			GetNbr(LPCTSTR str) = 0;
		// return number seconds from str example : "5m" returns 300
		virtual DWORD			GetSeconds(LPCTSTR str) = 0;
		// return a setting from the bbwin main configuration
		virtual const std::string		 & GetSetting(LPCTSTR settingName) = 0;
		
		//
		// stopping handles count : Only useful for agent using multiple threading or Wait functions
		//
		virtual void	GetHandles(HANDLE * hEvents) = 0;
		virtual DWORD	GetHandlesCount() = 0;
		
		// Hobbit Centralized Mode
		// if it returns true, then the agent will have to use ClientData instead of Status
		// 
		virtual bool	IsCentralModeEnabled() = 0;

		//
		//
		// Configuration Loading Abstration Tools
		//
		// loading configuration functions on default bbwin configuration file bbwin.cfg : return true if success
		virtual PBBWINCONFIG LoadConfiguration(LPCTSTR nameSpace) = 0;
		// loading configuration functions on custom configuration file 
		virtual PBBWINCONFIG LoadConfiguration(LPCTSTR fileName, LPCTSTR nameSpace) = 0;
		// free the configuration instance
		virtual void	FreeConfiguration(PBBWINCONFIG conf) = 0;
		// get a range of configuration settings
		virtual PBBWINCONFIGRANGE   GetConfigurationRange(PBBWINCONFIG conf, LPCTSTR name) = 0;
		// iterate the range of configuration settings : return true until the end of the range is reached
		virtual bool				IterateConfigurationRange(PBBWINCONFIGRANGE range) = 0;
		// return true if the end of the configuration range has been reached
		virtual bool				AtEndConfigurationRange(PBBWINCONFIGRANGE range) = 0;
		// get a value from a bbwin_config_range
		virtual LPCTSTR				GetConfigurationRangeValue(PBBWINCONFIGRANGE range, LPCTSTR name) = 0;
		// free the range of configuration settings
		virtual void				FreeConfigurationRange(PBBWINCONFIGRANGE range) = 0;

		// Report Functions : report in the bbwin log file
		virtual void 	Log(const int level, LPCTSTR str, ...) = 0;
		
		// Event Report Functions : report in the Windows event log
		virtual void 	ReportEventError(LPCTSTR str) = 0;
		virtual void 	ReportEventInfo(LPCTSTR str) = 0;
		virtual void 	ReportEventWarn(LPCTSTR str) = 0;
		
		// hobbit protocol
		virtual void 	Status(LPCTSTR testName, LPCTSTR color, LPCTSTR text, LPCTSTR lifeTime = "") = 0;
		virtual void	Notify(LPCTSTR testName, LPCTSTR text) = 0;
		virtual void	Data(LPCTSTR dataName, LPCTSTR text) = 0;
		virtual void 	Disable(LPCTSTR testName, LPCTSTR duration, LPCTSTR text) = 0;
		virtual void	Enable(LPCTSTR testName) = 0;
		virtual void	Drop() = 0;
		virtual void	Drop(LPCTSTR testName) = 0;
		virtual void	Rename(LPCTSTR newHostName) = 0;
		virtual void	Rename(LPCTSTR oldTestName, LPCTSTR newTestName) = 0;
		virtual void	Message(LPCTSTR message, LPTSTR dest, DWORD size) = 0;
		virtual void	Config(LPCTSTR fileName, LPCTSTR dest) = 0;
		virtual void	Query(LPCTSTR testName, LPTSTR dest, DWORD size) = 0;
		virtual void	Download(LPCTSTR fileName, LPCTSTR dest) = 0;
		virtual void	ClientData(LPCTSTR dataName, LPCTSTR text) = 0;

		// virtual destructor 
		virtual ~IBBWinAgentManager() {};
};


#endif // !__IBBWINAGENTMANAGER_H__
