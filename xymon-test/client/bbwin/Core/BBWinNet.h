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

#ifndef __BBWINNETOBJ_H__
#define __BBWINNETOBJ_H__

#define BB_TCP_PORT 	"1984"
#define PROXY_PORT 		"8080"
#define BB_LEN_RECV	1024
#define BB_LEN_SEND	1024

#include "IBBWinException.h"

//
// BBWinNet is used to provide easy hobbit protocol access
//
//
class BBWinNet 
{
	private :
	// hobbit protocol variables
	std::string		m_port;
	std::string		m_bbDisplay;
	std::string 	m_hostName;
	std::string		m_proxyHost;
	std::string		m_proxyPort;
	std::string		m_proxyUser;
	std::string		m_proxyPass;
	bool			m_debug;
	
	// network variables
	WSADATA 	m_wsaData;
	SOCKET 		m_conn;
	bool		m_connected;
	
	private :
		void		Open();
		void 		Close();
		void		Connect(const std::string & host, const std::string & port);
		void		Send(const std::string & message);
		void		Init();
		void		ConnectProxy();
	public :
		BBWinNet();
		BBWinNet(const std::string & hostName);
		~BBWinNet();
		
		void			SetHostName(const std::string & hostName);
		const std::string 	& GetHostName();
		void			SetBBDisplay(const std::string & bbDisp);
		const std::string	& GetBBDisplay();
		void			SetBBPager(const std::string & bbPager) { SetBBDisplay(bbPager); };
		const std::string	& GetBBPager() {return GetBBDisplay();};	
		
		void			SetProxy(const std::string & proxy);

		void			SetPort(const std::string & port);
		const std::string &	GetPort();
		
		void			SetDebug(bool debug);
		
		void 		Status(const std::string & testName, const std::string & color, const std::string & text, const std::string & lifetime = "");
		void 		Pager(const std::string & testName, const std::string & color, const std::string & text, const std::string & lifetime = "");
		void		Notify(const std::string & testName, const std::string & text);
		void		Data(const std::string & dataName, const std::string & text);
		void 		Disable(const std::string & testName, const std::string & duration, const std::string & text);
		void		Enable(const std::string & testName);
		void		Drop();
		void		Drop(const std::string & testName);
		void		Rename(const std::string & newHostName);
		void		Rename(const std::string & oldTestName, const std::string & newTestName);
		void		Test();
		
		void		Message(const std::string & message, std::string & dest);
		void		Config(const std::string & fileName, const std::string & destPath);
		void		Query(const std::string & testName, std::string & dest);
		void		Download(const std::string & fileName, const std::string & destPath);
		void		ClientData(const std::string & reportPath, const std::string & destPath);
};

/** class BBWinNetException 
*/
class BBWinNetException : IBBWinException {
public:
	BBWinNetException(const char* m);
	std::string getMessage() const;
};	

#endif // __BBWINNETOBJ_H__
