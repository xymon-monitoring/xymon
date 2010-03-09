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
// $Id: BBWin.h 96 2008-05-21 17:30:49Z sharpyy $

#include <map>
#include <string>
using namespace std;

#ifndef 	__BBWIN_H__
#define		__BBWIN_H__

#define BB_PATH_LEN		4096

#include "IBBWinException.h"

#include "Singleton.h"

using namespace DesignPattern;

#define	BBWIN_CONFIG_FILENAME		"bbwin.cfg"
#define BBWIN_NAMESPACE_CONFIG		"bbwin"

#define BBWIN_LOG_DEFAULT_PATH		"C:\\BBWin.log"

#define	BBWIN_STOP_HANDLE			0
#define BBWIN_MAX_COUNT_HANDLE		1

#define BBWIN_AUTORELOAD_INTERVAL	5

#include "Logging.h"
#include "BBWinHandler.h"
#include "BBWinLocalHandler.h"
#include "BBWinCentralHandler.h"
#include "BBWinConfig.h"

// declaration for the pointer function
class BBWin;
// pointer treatment function 
typedef void (BBWin::*load_simple_setting)(const std::string & name, const std::string & value);


class BBWin : public Singleton< BBWin > {
	private :
		// Configuration and Logs
		Logging					*m_log;
		bbwinconfig_t			m_configuration;
		BBWinConfig				*m_conf;

		// stop variables
		bool					m_do_stop; 
		void					(*m_report_stop)(void);

		// BBWin Handle 
		HANDLE					m_hEvents[BBWIN_MAX_COUNT_HANDLE];
		DWORD					m_hCount;

		// Autoreload feature
		FILETIME				m_confTime;
		bool					m_autoReload;
		DWORD					m_autoReloadInterval;
		
		// Hobbit client mode
		bool					m_centralMode;
		
		//
		// STL Containers
		//
		std::map< std::string, BBWinHandler * >			m_agents; // handle the agents instance
		std::map< std::string, BBWinLocalHandler *>		m_localHandlers; // handle to the
		BBWinCentralHandler								*m_centralClient; // central handler for hobbit client

		// list of the local handler threads

		std::vector< std::string >						m_bbdisplay; // handle the bbdisplay list
		std::vector< std::string >						m_bbpager; // handle the bbpager list
		std::map< std::string, std::string >			m_setting; // handle the general settings
		std::map< std::string, load_simple_setting > 	m_defaultSettings; // handle the default settings
		
	private :
		void			BBWinRegQueryValueEx(HKEY hKey, const std::string & key, std::string & dest);
		
		void 			ReportEvent(DWORD dwEventID, WORD cInserts, LPCTSTR *szMsg);
		void			Init(HANDLE h);
		void			InitSettings();
		void			WaitFor();
		
		void			callback_AddSimpleSetting(const std::string & name, const std::string & value);
		void			callback_AddBBDisplay(const std::string & name, const std::string & value);
		void			callback_AddBBPager(const std::string & name, const std::string & value);
		void			callback_SetAutoReload(const std::string & name, const std::string & value);
		void			callback_SetBBWinMode(const std::string & name, const std::string & value);

		void			SetAutoReload(bool value);

		bool			GetConfFileChanged();
		
		void 			LoadRegistryConfiguration();
		void			LoadConfiguration();
		void			CheckConfiguration();
		
		void			StartAgents();
		void			StopAgents();

		void			LoadAgents();
		void			UnloadAgents();
		
		// private start function used at startup and reloading
		void			_Start();

	public :
		BBWin();
		~BBWin();
		void Start(HANDLE h);
		void Reload();
		void Stop(void (*report_stop)(void));
};


/** class BBWinException 
*/
class BBWinException : public IBBWinException {
public:
	BBWinException(const char* m);
	string getMessage() const;
};	


#endif // !__BBWIN_H__
