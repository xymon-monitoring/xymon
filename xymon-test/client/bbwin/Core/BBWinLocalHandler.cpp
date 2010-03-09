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
// $Id: BBWinLocalHandler.cpp 96 2008-05-21 17:30:49Z sharpyy $

#include <windows.h>

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
using namespace std;

#include "ou_thread.h"
using namespace openutils;

#include "BBWinNet.h"
#include "BBWinHandler.h"
#include "BBWinLocalHandler.h"
#include "BBWinMessages.h"

#include "Utils.h"
using namespace utils;



//
//  FUNCTION: BBWinLocalHandler
//
//  PURPOSE: constructor
//
//  PARAMETERS:
//    
//    
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
//
BBWinLocalHandler::BBWinLocalHandler(BBWinHandler & handler) :
							m_handler (handler)
{
	Thread::setName(handler.GetAgentName().c_str());
}


//
//  FUNCTION: BBWinLocalHandler::run
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
//  thread run function. Thread is terminated at the end of this function
//
void BBWinLocalHandler::run() {
	
	m_handler.Run();

}


// BBWinHandlerException
BBWinLocalHandlerException::BBWinLocalHandlerException(const char* m) {
	msg = m;
}

string BBWinLocalHandlerException::getMessage() const {
	return msg;
}


