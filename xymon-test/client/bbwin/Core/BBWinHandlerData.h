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
// $Id: BBWinHandlerData.h 96 2008-05-21 17:30:49Z sharpyy $

#ifndef __BBWINHANDLERDATA_H__
#define __BBWINHANDLERDATA_H__

#include <windows.h>

#include <string>
#include <map>
#include <vector>

typedef std::vector< std::string >					bbdisplay_t;
typedef std::vector< std::string >					bbpager_t;
typedef std::map< std::string, std::string >		setting_t;

//
// simple data struct which old everything needed for agents
// this file is separated from BBWinHandler.h because it is also use 
// to the facade class BBWinAgentManager
//
typedef struct 				bbwinhandler_data_s 
{
	HANDLE 					*hEvents;
	DWORD 					hCount;
	std::string				& agentName;
	std::string				& agentFileName;
	bbdisplay_t				& bbdisplay;
	bbpager_t				& bbpager;
	setting_t				& setting;
	DWORD					timer;
}							bbwinhandler_data_t;


#endif // !__BBWINHANDLERDATA_H__
