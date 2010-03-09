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
//
// $Id: Utils.h 76 2008-02-07 14:15:12Z sharpyy $

#ifndef		__UTILS_H__
#define		__UTILS_H__

#define _SECOND ((int64) 10000000)
#define _MINUTE (60 * _SECOND)
#define _HOUR   (60 * _MINUTE)
#define _DAY    (24 * _HOUR)

#include <string>
#include "ProcInOut.h"

//
// PURPOSE : BBWin utils functions
//
namespace	utils {

DWORD			GetSeconds(const std::string & str );
LPTSTR 			GetLastErrorText( LPTSTR lpszBuf, DWORD dwSize );
void			GetLastErrorString(std::string & str);
DWORD			GetNbr(const std::string & str );
void			GetEnvironmentVariable(const std::string & varname, std::string & dest);
void			ReplaceEnvironmentVariableStr(std::string & str);
bool			parseStrGetNext(const std::string & str, const std::string & match, std::string & next);
bool			parseStrGetLast(const std::string & str, const std::string & match, std::string & last);
void			SystemTimeToTime_t(SYSTEMTIME *systemTime, time_t *dosTime);
void			RemoveComments(const std::string &src, std::string &dest, const std::string & separator);
void			GetConfigLine(std::ifstream & conf, std::string & str);

}

#endif // !__UTILS_H__

