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
// $Id: Logging.h 96 2008-05-21 17:30:49Z sharpyy $

#ifndef __LOGGING_H__
#define __LOGGING_H__

#include "IBBWinException.h"

#include "Singleton.h"

using namespace DesignPattern;

#define			LOGLEVEL_ERROR		1
#define			LOGLEVEL_WARN		2
#define 		LOGLEVEL_INFO		3
#define			LOGLEVEL_DEBUG		4

#define			LOGLEVEL_DEFAULT	2

//
// class used to log information
// singleton
class Logging : public Singleton< Logging > {
	private :
		int						m_logLevel;
		std::string				m_fileName;
		FILE					*m_fileHandle;

	protected :
		void	open();
		void 	close();
		void 	write(LPCTSTR log, va_list ap);
		void 	reportEvent(WORD type, WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str);
	
	public :
		Logging();
		~Logging();
	
		void setLogLevel(DWORD level);
		void setFileName(const std::string & fileName);
		void log(const int level, LPCTSTR log, ...);
		void vlog(const int level, LPCTSTR log, va_list ap);
		void reportInfoEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str);
		void reportSuccessEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str);
		void reportErrorEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str);
		void reportWarnEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str);
		
};

/** class LoggingException 
*/
class LoggingException : public IBBWinException {
public:
	LoggingException(const char* m);
	std::string getMessage() const;
};	

#endif // __LOGGING_H__

