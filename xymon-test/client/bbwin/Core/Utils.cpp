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
// $Id: Utils.cpp 76 2008-02-07 14:15:12Z sharpyy $

#include <windows.h>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include "Utils.h"

using namespace std;

namespace	utils {


typedef struct 	duration_s {
	LPCTSTR		letter;
	DWORD		duration;
}				duration_t;

// The LIFETIME is in minutes, unless you add an "h" (hours), "d" (days) or "w" (weeks) immediately after the number

static 	const duration_t		duration_table[] = 
{
	{"s", 1}, 			// s for seconds
	{"m", 60}, 			// m for minutes
	{"h", 3600},		// h for hours
	{"d", 86400},		// d for days
	{"w", 604800},		// w for weeks
	{NULL, 0}
};

//
//  FUNCTION: GetSeconds
//
//  PURPOSE: return seconds from duration string
//
//  PARAMETERS:
//    str 		string contening the seconds number as "65", "100m", "6d" for 6 days
//
//  RETURN VALUE:
//    DWORD seconds number
//
//  COMMENTS:
// 
//
DWORD			GetSeconds(const string & str ) {
	DWORD		sec;
	size_t		let;
	string		dup = str;
	bool		noTimeIndic = true;
	
	sec = 0;
	for (int i = 0; duration_table[i].letter != NULL; i++) {
		let = str.find(duration_table[i].letter);
		if (let > 0 && let < str.size()) {
			istringstream iss(str.substr(0, let));
			iss >> sec;
			sec *= duration_table[i].duration;
			noTimeIndic = false;
			break ;
		} 
	}
	if (noTimeIndic == true) {
		istringstream iss(str.substr(0, let));
		iss >> sec;
	}
	return sec;
}

//
//  FUNCTION: GetLastErrorText
//
//  PURPOSE: copies error message text to char *
//
//  PARAMETERS:
//    lpszBuf - destination buffer
//    dwSize - size of buffer
//
//  RETURN VALUE:
//    destination buffer
//
//  COMMENTS:
//
LPTSTR GetLastErrorText( LPTSTR lpszBuf, DWORD dwSize )
{
   DWORD dwRet;
   LPTSTR lpszTemp = NULL;

   dwRet = FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |FORMAT_MESSAGE_ARGUMENT_ARRAY,
                          NULL,
                          GetLastError(),
                          LANG_NEUTRAL,
                          (LPTSTR)&lpszTemp,
                          0,
                          NULL );
   // supplied buffer is not long enough
	if ( !dwRet || ( (long)dwSize < (long)dwRet+14 ) ) {
		lpszBuf[0] = TEXT('\0');
	} else {
		lpszTemp[lstrlen(lpszTemp)-2] = TEXT('\0');  //remove cr and newline character
		strcpy(lpszBuf, lpszTemp);
	}
	if ( lpszTemp )
		LocalFree((HLOCAL) lpszTemp );
	return lpszBuf;
}


//
//  FUNCTION: GetLastErrorString
//
//  PURPOSE: copies error message text to string
//
//  PARAMETERS:
//    str - string destination
//
//  RETURN VALUE:
//   none
//
//  COMMENTS:
// 
//  wrapper method to GetLastErrorText
// 
#define		MAX_OUTPUT_ERROR			1024

void		GetLastErrorString(string & str) {
	char	buf[MAX_OUTPUT_ERROR + 1];
	
	GetLastErrorText(buf, MAX_OUTPUT_ERROR);
	str = buf;
}

//
//  FUNCTION: GetNbr
//
//  PURPOSE: return number from a string
//
//  PARAMETERS:
//    str 		string contening the  number
//
//  RETURN VALUE:
//    DWORD 	number
//
//  COMMENTS:
// 
//
DWORD			GetNbr(const string & str ) {
	DWORD		nbr;

	istringstream iss(str);
	iss >> nbr;
	return nbr;
}

//
//  FUNCTION: GetEnvironmentVariable
//
//  PURPOSE: get the environment variable value from its name
//
//  PARAMETERS:
//    varname 		name of the environment variable
//    dest			destination value
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
// 
//
void			GetEnvironmentVariable(const string & varname, string & dest) {
	DWORD 		dwRet;
	char		buf[MAX_PATH + 1];
 
	dwRet = ::GetEnvironmentVariable(varname.c_str(), buf, MAX_PATH);
	if (dwRet != 0) {
		dest = buf;
	}
}

//
//  FUNCTION: ReplaceEnvironmentVariableStr
//
//  PURPOSE: replace all environments variables names with their value
//
//  PARAMETERS:
//    str 		string contening the  number
//
//  RETURN VALUE:
//    DWORD 	number
//
//  COMMENTS:
// 
//
void			ReplaceEnvironmentVariableStr(string & str) {
	size_t		begin, end;
	string		var;
	
	begin = str.find_first_of("%");
	if (begin >= 0 && begin < (str.length() - 1)) {
		end = str.find_first_of("%", begin + 1);
		if (end >= (begin + 1) && end < str.length()) {
			GetEnvironmentVariable(str.substr(begin + 1, end - (begin + 1)),  var);
			str.erase(begin, end + 1);
			str.insert(begin, var);
			ReplaceEnvironmentVariableStr(str);
		}
	}
}

bool		parseStrGetNext(const std::string & str, const std::string & match, std::string & next) {
	std::string::size_type		res = str.find(match);

	if (res >= 0 && res < str.size()) {
		next = str.substr(match.size());
		return true;
	}
	return false;
}

bool		parseStrGetLast(const std::string & str, const std::string & match, std::string & last) {
	std::string::size_type		res = str.find_last_of(match);

	if (res >= 0 && res < str.size()) {
		last = str.substr(res + 1);
		return true;
	}
	return false;
}

//
// from http://blogs.msdn.com/joshpoley/archive/2007/12/19/date-time-formats-and-conversions.aspx
//
void SystemTimeToTime_t(SYSTEMTIME *systemTime, time_t *dosTime) {
	LARGE_INTEGER jan1970FT = {0};
	
	jan1970FT.QuadPart = 116444736000000000I64; // january 1st 1970
    LARGE_INTEGER utcFT = {0};
    SystemTimeToFileTime(systemTime, (FILETIME*)&utcFT);
    unsigned __int64 utcDosTime = (utcFT.QuadPart - jan1970FT.QuadPart)/10000000;
    *dosTime = (time_t)utcDosTime;
}

void RemoveComments(const std::string &src, std::string &dest, const std::string & separator) {
	size_t		res = src.find_first_of(separator);
	
	if (res >= 0 && res < src.size()) {
		dest = src.substr(0, res);
	} else {
		dest = src;
	}
}

void			GetConfigLine(std::ifstream & conf, std::string & str) {
	std::string		cleanstr;
	std::string		buf;

	std::getline(conf, buf);
	RemoveComments(buf, cleanstr, "#");
	str = cleanstr;
}

} // namespace utils

