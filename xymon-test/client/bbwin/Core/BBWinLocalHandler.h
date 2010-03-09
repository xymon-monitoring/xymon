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
// $Id: BBWinLocalHandler.h 96 2008-05-21 17:30:49Z sharpyy $

#ifndef __BBWINLOCALHANDLER_H__
#define __BBWINLOCALHANDLER_H__


#include "IBBWinException.h"
#include "BBWinAgentManager.h"
#include "IBBWinAgent.h"
#include "Logging.h"

#include "BBWinHandlerData.h"

// 
// inherit from the nice thread class written by Vijay Mathew Pandyalakal
// this class handle each agent and execute agent code
// 
class BBWinLocalHandler : public Thread {
	private:
		BBWinHandler &		m_handler;

	
	public:
		BBWinLocalHandler(BBWinHandler & handler);
		void run();
};


/** class LoggingException 
*/
class BBWinLocalHandlerException : IBBWinException {
public:
	BBWinLocalHandlerException(const char* m);
	string getMessage() const;
};	




#endif // __BBWINLOCALHANDLER_H__

