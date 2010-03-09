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
// $Id: EventLog.cpp 79 2008-02-08 15:03:16Z sharpyy $

#include <windows.h>
#include <assert.h>
#include <string>    // inclut aussi <cctype> et donc tolower
#include <sstream>
#include <fstream>
#include <iostream>  // pour cout
#include <algorithm> // pour transform
#include <boost/regex.hpp>
#include "boost/format.hpp"
#include "EventLog.h"

using namespace boost;
using namespace std;
using namespace EventLog;
using boost::format;

//
// common bb colors
//
static const char	*bbcolors[] = {"green", "yellow", "red", NULL};

static const struct _eventType { 
	std::string		name;
	WORD			value;
} eventType[] = 
{
	{"error", EVENTLOG_ERROR_TYPE},
	{"failure", EVENTLOG_AUDIT_FAILURE},
	{"success", EVENTLOG_AUDIT_SUCCESS},
	{"information", EVENTLOG_INFORMATION_TYPE},
	{"warning", EVENTLOG_WARNING_TYPE},
	{"", 0}
};

//
// generic method
//

static void			MyGetEnvironmentVariable(const string & varname, string & dest) {
	DWORD 		dwRet;
	char		buf[REG_BUF_SIZE + 1];
 
	SecureZeroMemory(buf, sizeof(buf));
	dwRet = ::GetEnvironmentVariable(varname.c_str(), buf, MAX_PATH);
	if (dwRet != 0) {
		dest = buf;
	}
}

static const string & GetEventTypeStr(WORD type) {
	static const string nofound = "unknown type";

	for (DWORD inc = 0; eventType[inc].value != 0; ++inc) {
		if (eventType[inc].value == type)
			return eventType[inc].name;
	}
	return nofound;
}

static void			ReplaceEnvVariable(string & str) {
	size_t		resFirst, resSecond;
	string		envname;
	
	resFirst = str.find_first_of("%");
	if (resFirst >= 0 && resFirst < str.size()) {
		str.erase(resFirst, 1);
		resSecond = str.find_first_of("%");
		if (resSecond >= 0 && resSecond < str.size()) {
			string			var;

			envname = str.substr(resFirst, resSecond);
			str.erase(resSecond, 1);
			str.erase(resFirst, resSecond);
			MyGetEnvironmentVariable(envname, var);
			str.insert(resFirst, var);
			ReplaceEnvVariable(str);
		}
	}
}

static void				Convert1970ToSystemTime(LONG seconds, SYSTEMTIME * myTime) {
	FILETIME		fileTime;
	SYSTEMTIME		origin;

	assert(myTime != NULL);
	ZeroMemory(&fileTime, sizeof(fileTime));
	ZeroMemory(myTime, sizeof(*myTime));
	ZeroMemory(&origin, sizeof(origin));
	origin.wYear = 1970;
	origin.wMonth = 1;
	origin.wDay = 1;
	origin.wSecond = 1;
	origin.wHour = 0;
	origin.wMilliseconds = 0;
	origin.wMinute = 0;
	if (!SystemTimeToFileTime(&origin, &fileTime))
		return ;
	ULARGE_INTEGER	ulTmp, ulOrigin;
	ulOrigin.HighPart = fileTime.dwHighDateTime;
	ulOrigin.LowPart = fileTime.dwLowDateTime;
	ulTmp.QuadPart = (ulOrigin.QuadPart + (seconds * 10000000)); 
	ZeroMemory(&fileTime, sizeof(fileTime));
	fileTime.dwHighDateTime = ulTmp.HighPart;
	fileTime.dwLowDateTime = ulTmp.LowPart;
	FileTimeToSystemTime(&fileTime, myTime);
}


static void		TimeToFileTime(LONG t, LPFILETIME pft) {
	// Note that LONGLONG is a 64-bit value
	LONGLONG ll;

	ll = Int32x32To64(t, 10000000) + 116444736000000000;
	pft->dwLowDateTime = (DWORD)ll;
	pft->dwHighDateTime = (DWORD)(ll >> 32);
}

static void		TimeToSystemTime(LONG t, LPSYSTEMTIME pst) {
	FILETIME ft;

	TimeToFileTime(t, &ft);
	FileTimeToSystemTime(&ft, pst);
}

static void replace_all(string& str, const string& fnd, const string& rep ) {
	std::string::size_type pos = 0;
	std::string::size_type len = fnd.length();
	std::string::size_type rep_len = rep.length();

	while((pos = str.find(fnd,pos)) != std::string::npos) {
		str.replace(pos, len, rep);
		pos += rep_len;
	} 
} 


static void		clean_spaces(string & str) {
	std::string::size_type		begin = 0;
	std::string::size_type		end = 0;

	while ((begin = str.find("  ", begin)) != std::string::npos) {
		begin += 1;
		end = str.find_first_not_of(" ", begin);
		if (end != std::string::npos) {
			str.erase(begin, end - begin);
		}
	}
}


//
// Rule methods
//

Rule::Rule() :
		m_useId(false),
		m_id(0),
		m_alarmColor(1),
		m_ignore(false),
		m_type(0),
		m_delay(30 * 60),
		m_count(0),
		m_countTmp(0),
		m_priority(0)
{

}

Rule::~Rule() {
		
}

Rule::Rule(const Rule & rule) {
	m_useId = false;
	m_id = rule.GetEventId();
	if (m_id > 0)
		m_useId = true;
	m_source = rule.GetSource();
	m_alarmColor = rule.GetAlarmColor();
	m_ignore = rule.GetIgnore();
	m_type = rule.GetType();
	m_user = rule.GetUser();
	m_value = rule.GetValue();
	m_delay = rule.GetDelay();
	m_count = rule.GetCount();
	m_countTmp = 0;
	m_priority = rule.GetPriority();
}

void		Rule::SetSource(const std::string & source) { 
	m_source = source; 
	std::transform( m_source.begin(), m_source.end(), m_source.begin(), tolower ); 
} 


void		Rule::SetValue(const std::string & value) { 
	m_value = value; 
} 

void		Rule::SetType(WORD type) { 
	m_type = type; 
}


// set the type field
// return false if it is an unknown type
bool		Rule::SetType(const std::string &type) {
	DWORD	inc;
	size_t	res;

	for (inc = 0; eventType[inc].value != 0; ++inc) {
		res = eventType[inc].name.find(type);
		if (res >= 0 && res < eventType[inc].name.size()) {
			m_type = eventType[inc].value;
			return true;
		}
	}
	return false;
}


//
// Session methods
//

Session::Session(const std::string logfile) :
		m_logfile(logfile),
		m_maxDelay(0),
		m_maxData(10240),
		m_centralized(false)
{
	InitCounters();
}


Session::~Session() {

}

LONG			Session::Now() {
	SYSTEMTIME	sysTime;
	FILETIME	fileTime;

	ZeroMemory(&sysTime, sizeof(sysTime));
	ZeroMemory(&fileTime, sizeof(fileTime));
	GetSystemTime(&sysTime);
	if (!SystemTimeToFileTime(&sysTime, &fileTime))
		return 0;
	ULARGE_INTEGER	ulNow, ulOrigin;
	SYSTEMTIME		origin;
	ZeroMemory(&origin, sizeof(origin));
	ulNow.HighPart = fileTime.dwHighDateTime;
	ulNow.LowPart = fileTime.dwLowDateTime;
	origin.wYear = 1970;
	origin.wMonth = 1;
	origin.wDay = 1;
	origin.wSecond = 1;
	origin.wHour = 0;
	origin.wMilliseconds = 0;
	origin.wMinute = 0;
	if (!SystemTimeToFileTime(&origin, &fileTime))
		return 0;
	ulOrigin.HighPart = fileTime.dwHighDateTime;
	ulOrigin.LowPart = fileTime.dwLowDateTime;
	LONG sec = (LONG)((ulNow.QuadPart - ulOrigin.QuadPart) / 10000000);
	return sec;
}


void			Session::AddRule(const Rule & rule) {
	if (rule.GetIgnore())
		m_ignoreRules.push_back(rule);
	else
		m_matchRules.push_back(rule);
}

// get the max delay
DWORD			Session::GetMaxDelay() {
	DWORD						delay = 0;
	std::list<Rule>::iterator	itr;
	
	for (itr = m_matchRules.begin(); itr != m_matchRules.end(); ++itr) {
		if ((*itr).GetDelay() > delay) {
			delay = (*itr).GetDelay();
		}
	}
	return delay;
}

void			Session::FreeSources() {
	eventlog_mod_itr_t		itr;

	for (itr = m_sources.begin(); itr != m_sources.end(); ++itr) { 
		FreeLibrary((*itr).second);
	}
	m_sources.clear();
}


void			Session::LoadEventMessageFile(const string & source, const EVENTLOGRECORD * ev) {
	stringstream 		regkey;	
	HKEY 				hk; 
	TCHAR				buf[REG_BUF_SIZE + 1];
	DWORD				bufsize = REG_BUF_SIZE;
	unsigned long 		lDataType;

	regkey << EVENT_LOG_REG_KEY << "\\" << m_logfile << "\\" << source;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, regkey.str().c_str(), 0, KEY_READ, &hk))
	{
		return ;
	}
	SecureZeroMemory(buf, sizeof(buf));
	if (RegQueryValueEx(hk, "EventMessageFile", NULL, &lDataType, (LPBYTE)buf, &bufsize) != ERROR_SUCCESS) {
		RegCloseKey(hk);
		return ;
	}
	RegCloseKey(hk); 
	istringstream	iss(buf);
    string			module;
	while ( std::getline( iss, module, ';' ) )
    {
		ReplaceEnvVariable(module);
		HMODULE		hm;
	
		hm = LoadLibraryEx(module.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE);
		if (hm != NULL) {
			m_sources.insert(pair<string, HMODULE>(source, hm));
		}
    }
}

void 					Session::GetEventUser(const EVENTLOGRECORD * ev, std::string & user) {
	TCHAR				userbuf[ACCOUNT_SIZE];
	TCHAR				domainbuf[ACCOUNT_SIZE];
	DWORD				userSize, domainSize;
	SID_NAME_USE		type;
	
	userSize = ACCOUNT_SIZE;
	domainSize = ACCOUNT_SIZE;
	SecureZeroMemory(userbuf, ACCOUNT_SIZE);
	SecureZeroMemory(domainbuf, ACCOUNT_SIZE);
	BOOL res = LookupAccountSid(NULL, (SID *)((LPBYTE)ev + ev->UserSidOffset), userbuf, &userSize,
		domainbuf, &domainSize, &type);
	if (res) {
		stringstream 		username;	

		username << domainbuf << "\\" << userbuf;
		user = username.str();
	} else {
		user = UNKOWN_ACCOUNT;
	}
}


// when the message DLL is not present or the evenid description not found in the message DLL
// it simulates the Windows Event Viewer behavior
static void				FillUnresolvedDescription(const EVENTLOGRECORD * ev, const LPTSTR *strarray, std::string & description) {
	stringstream		reportDesc;
	
	reportDesc << "The description for Event ID ( " << (ev->EventID & MSG_ID_MASK) << " ) ";
	reportDesc << "in Source ( " << (LPSTR) ((LPBYTE) ev + sizeof(EVENTLOGRECORD)) << " ) ";
	reportDesc << "cannot be found. The local computer may not have the necessary registry ";
	reportDesc << "information or message DLL files to display messages from a remote computer. ";
	reportDesc << "You may be able to use the /AUXSOURCE= flag to retrieve this description; see ";
	reportDesc << "Help and Support for details. The following information is part of the event: ";
	for (DWORD count = 0; strarray[count] != NULL; count++) {
		if (count > 0)
			reportDesc << "; ";
		reportDesc << strarray[count];
	}
	reportDesc << ".";
	description = reportDesc.str();
}

void				Session::GetEventDescription(const EVENTLOGRECORD * ev, std::string & description) {
	string				source =  (LPSTR) ((LPBYTE) ev + sizeof(EVENTLOGRECORD));
	bool				generated = false;
	LPTSTR				strarray[MAX_STRING_EVENT + 1];

	SecureZeroMemory(strarray, sizeof(strarray));
	if (ev->NumStrings > 0 && ev->NumStrings < MAX_STRING_EVENT) {
		LPTSTR	str = NULL;
		DWORD	inc;

		str = (LPTSTR)((LPBYTE)ev + ev->StringOffset);
		for (inc = 0; inc < ev->NumStrings; inc++) {
			strarray[inc] = str;
			str = str + (strlen(str) + 1);
		}
		strarray[inc] = NULL;
	}
	eventlog_mode_range_t	range = m_sources.equal_range(source);
	if (range.first == range.second) {
		LoadEventMessageFile(source, ev);
		range = m_sources.equal_range(source);
		if (range.first == range.second) {
			// the message DLL is not present
			FillUnresolvedDescription(ev, strarray, description);
			return ;
		}
	}
	for ( ; range.first != range.second && generated == false; ++range.first) { 
		DWORD	dwRet;
		TCHAR	sBuf[DESC_BUF_SIZE + 1]; 
		DWORD	dwSize = DESC_BUF_SIZE;
		LPTSTR	lpszTemp = NULL;
		
		LPTSTR	*pstrarray = NULL;
		DWORD	dflags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ARGUMENT_ARRAY;
		SecureZeroMemory(sBuf, sizeof(sBuf));
		if (ev->NumStrings > 0 && ev->NumStrings < MAX_STRING_EVENT) {
			pstrarray = strarray;
		} else {
			dflags |= FORMAT_MESSAGE_IGNORE_INSERTS;
		}
		dwRet = FormatMessage(
				dflags,
				(LPCVOID)range.first->second,
				ev->EventID,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR)&lpszTemp,
				0,
				(va_list*) strarray);
		if ( !dwRet || ( (long)dwSize < (long)dwRet+14 ) ) {
			// the message DLL is present but the description for the event id is not included
			FillUnresolvedDescription(ev, strarray, description);
		} else {
			strcpy(sBuf, lpszTemp);
			description = sBuf;
			std::replace(description.begin(), description.end(), '\n', ' ');
			std::replace(description.begin(), description.end(), '\r', ' ');
			std::replace(description.begin(), description.end(), '\t', ' ');
			description.erase(description.find_last_not_of(' ') + 1); 
			clean_spaces(description);
			generated = true;
		}
		if ( lpszTemp ) {
			LocalFree((HLOCAL) lpszTemp );
			lpszTemp = NULL;
		}
	}
}

//
// return true if the event matched the rule
//
bool			Session::ApplyRule(const Rule & rule, const EVENTLOGRECORD * ev) {
	string test;

	GetEventUser(ev, test);
	if (rule.GetEventId() != 0 && rule.GetEventId() != (ev->EventID & MSG_ID_MASK))
		return false;
	if (ev->TimeGenerated < (m_now - rule.GetDelay()) 
		&& rule.GetIgnore() == false) // ignore rules don't depend on delay parameters
		return false;
	if (rule.GetSource().length() > 0) {
		string source = (LPSTR) ((LPBYTE) ev + sizeof(EVENTLOGRECORD));
		std::transform(source.begin(), source.end(), source.begin(), tolower);
		if (source != rule.GetSource())
			return false;
	}
	if (rule.GetType() != 0 && rule.GetType() != ev->EventType)
		return false;
	if (rule.GetUser().length() > 0) {
		string user;

		boost::regex e(rule.GetUser(), boost::regbase::perl);
		GetEventUser(ev, user);
		boost::match_results<std::string::const_iterator>	what;
		if(boost::regex_search(user, what, e) == 0)	{
			return false;
		} 
	}
	if (rule.GetValue().length() > 0) {
		string desc;
		
		boost::regex e(rule.GetValue(), boost::regbase::perl);
		GetEventDescription(ev, desc);
		boost::match_results<std::string::const_iterator>	what;
		if(boost::regex_search(desc, what, e) == 0)	{
			return false;
		} 
	}
	return true;
}

void			Session::InitCounters() {
	std::list<Rule>::iterator			itr;

	m_total = 0;
	m_match = 0;
	m_ignore = 0;
	m_numberOfRecords = 0;
	m_oldestRecord = 0;
	for (itr = m_matchRules.begin(); itr != m_matchRules.end(); ++itr) {
		(*itr).SetCurrentCount(0);
	}
}

static void			FormatEventDescription(string & desc) {
	size_t			resFirst;
	const size_t	width = 80;

	for (size_t pos = width; pos < desc.size(); pos += width) {
		resFirst = desc.find(" ", pos);
		if (resFirst >= 0 && resFirst < desc.size()) {
			desc.insert(resFirst, "\n");
		}
	}
}


// analyze a simple event with the match rules then the ignore rules
// return the status color
DWORD			Session::AnalyzeEvent(const EVENTLOGRECORD * ev, stringstream & reportData) {
	DWORD		priority = 0;
	std::list<Rule>::iterator			itr;
	bool		result;
	DWORD		color = BB_GREEN;	
	
	assert(ev != NULL);
	result = false;
	// check if one of the rules match the event
	for (itr = m_matchRules.begin(); itr != m_matchRules.end(); ++itr) {
		if (ApplyRule((*itr), ev)) {
			result = true;
			color = (*itr).GetAlarmColor();
			priority = (*itr).GetPriority();
			(*itr).IncrementCurrentCount();
			if ((*itr).GetCount() != 0 && (*itr).GetCount() > (*itr).GetCurrentCount())
				result = false;
			break ;
		}
	}
	if (result == false && m_centralized == false) // not matched
		return BB_GREEN;
	m_match++;
	// if one of the ignore rules matches, then the event is ignored
	for (itr = m_ignoreRules.begin(); itr != m_ignoreRules.end(); ++itr) {
		if (ApplyRule((*itr), ev)) {
			if ((*itr).GetPriority() != 0 && priority != 0 && (*itr).GetPriority() < priority) {
				break ;
			}
			m_ignore++;
			m_match--;
			result = false;
			break ;
		}
	}
	if (result == false && m_centralized == false)
		return BB_GREEN;
	string desc, user;
	GetEventDescription(ev, desc);
	GetEventUser(ev, user);
	const string & type = GetEventTypeStr(ev->EventType);
	SYSTEMTIME			stime, ltime;
	TCHAR				timebuf[TIME_BUF + 1];

	SecureZeroMemory(timebuf, sizeof(timebuf));
	TimeToSystemTime(ev->TimeGenerated, &stime);
	SystemTimeToTzSpecificLocalTime(NULL, &stime, &ltime);
	string				time, date;
	if (GetTimeFormat(LOCALE_SYSTEM_DEFAULT, TIME_FORCE24HOURFORMAT, &ltime, "hh':'mm':'ss", timebuf, TIME_BUF) != 0) {
		time = timebuf;
	} else {
		time = "00:00:00";
	}
	if (GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, &ltime, "yyyy/MM/dd", timebuf, TIME_BUF) != 0) {
		date = timebuf;
	} else {
		date = "unkwown";
	}
	if (result == true) {
		if (m_centralized == false)
			reportData << "&" << bbcolors[color] << " " << m_logfile << ": ";
		reportData << type << " - ";
		reportData << date << " " << time << " - ";
		reportData << (LPSTR) ((LPBYTE) ev + sizeof(EVENTLOGRECORD)) << " (" << (ev->EventID & MSG_ID_MASK) << ") - ";
		if (m_centralized == false) {
			reportData << user << " \n";
			FormatEventDescription(desc);
			reportData << " \"" << desc << "\"";
			reportData << "\n" << endl;
		} else {
			reportData << desc << endl;
		}
	}
	return color;
}


// Execute the rules for the logfile
//
// return the color
//
DWORD			Session::Execute(stringstream & reportData) {
	HANDLE					h;
	EVENTLOGRECORD			*pevlr; 
	BYTE					bBuffer[EVENT_BUFFER_SIZE]; 
	DWORD					dwRead, dwNeeded;
	DWORD					limit;
	DWORD					tmp, color = BB_GREEN;

	InitCounters();
	m_now = (DWORD)Now();
	m_maxDelay = GetMaxDelay();
	limit = m_now - m_maxDelay;
	h = OpenEventLog(NULL, m_logfile.c_str());   
	if (h == NULL)  {
		return color;
	}
	GetOldestEventLogRecord(h, &m_oldestRecord);
	GetNumberOfEventLogRecords(h, &m_numberOfRecords);
	pevlr = (EVENTLOGRECORD *) &bBuffer; 
	while (ReadEventLog(h,                // event log handle 
		EVENTLOG_BACKWARDS_READ |  // reads forward 
		EVENTLOG_SEQUENTIAL_READ, // sequential read 
		0,            // ignored for sequential reads 
		pevlr,        // pointer to buffer 
		EVENT_BUFFER_SIZE,  // size of buffer 
		&dwRead,      // number of bytes read 
		&dwNeeded))   // bytes in next record 
	{
		while (dwRead > 0 && pevlr->TimeGenerated >= limit) 
		{ 
			tmp = AnalyzeEvent(pevlr, reportData);
			if (tmp > color)
				color = tmp;
			m_total++;
			dwRead -= pevlr->Length;
			pevlr = (EVENTLOGRECORD *) 
				((LPBYTE) pevlr + pevlr->Length); 
		} 
		pevlr = (EVENTLOGRECORD *) &bBuffer; 
	} 
	CloseEventLog(h); 
	FreeSources();
	return color;
}


//
// Manager methods
//

	
Manager::Manager() : 
	m_checkFullLogFile(false),
	m_centralized(false)
{
	GetLogFileList();
}

Manager::~Manager() {
	FreeSessions();
}

void				Manager::FreeSessions() {
	std::map<std::string, Session *>::iterator	itr;
	
	for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr) {
		delete (*itr).second;
	}
	m_sessions.clear();
}

void		Manager::AddRule(const std::string & logfile, const Rule & rule) {
	std::string	logfilename = logfile;
	std::map<std::string, Session *>::iterator	itr;
	std::list< std::string >::iterator			logitr;
	std::string				originalname;
	size_t					res;
	
	std::transform(logfilename.begin(), logfilename.end(), logfilename.begin(), tolower);
	for (logitr = m_logFileList.begin(); logitr != m_logFileList.end(); ++logitr) {
		res = (*logitr).find(logfilename);
		if (res >= 0 && res < (*logitr).size()) 
			originalname = (*logitr);
	}
	if (originalname.size() == 0)  {
		return ;
	}
	itr = m_sessions.find(originalname);
	if (itr == m_sessions.end()) {
		Session		* ses;
		try {
			 ses = new Session(originalname);
		} catch (std::bad_alloc & ) {
			return ;
		}
		assert(ses != NULL);
		ses->AddRule(rule);
		m_sessions.insert(std::pair<std::string, Session*>(originalname, ses));
	} else {
		(*itr).second->AddRule(rule);
	}
}

void		Manager::SetMaxData(const std::string & logfile, DWORD maxData) {
	std::string	logfilename = logfile;
	std::map<std::string, Session *>::iterator	itr;
	std::list< std::string >::iterator			logitr;
	std::string				originalname;
	size_t					res;
	
	std::transform(logfilename.begin(), logfilename.end(), logfilename.begin(), tolower);
	for (logitr = m_logFileList.begin(); logitr != m_logFileList.end(); ++logitr) {
		res = (*logitr).find(logfilename);
		if (res >= 0 && res < (*logitr).size()) 
			originalname = (*logitr);
	}
	if (originalname.size() == 0)  {
		return ;
	}
	itr = m_sessions.find(originalname);
	if (itr != m_sessions.end()) {
		(*itr).second->SetMaxData(maxData);
	}
}

void		Manager::AddLogFile(const std::string & logfile) {
	std::string	logfilename = logfile;
	std::map<std::string, Session *>::iterator	itr;
	std::list< std::string >::iterator			logitr;
	std::string				originalname;
	size_t					res;
	
	std::transform(logfilename.begin(), logfilename.end(), logfilename.begin(), tolower);
	for (logitr = m_logFileList.begin(); logitr != m_logFileList.end(); ++logitr) {
		res = (*logitr).find(logfilename);
		if (res >= 0 && res < (*logitr).size()) 
			originalname = (*logitr);
	}
	if (originalname.size() == 0)  {
		return ;
	}
	itr = m_sessions.find(originalname);
	if (itr == m_sessions.end()) {
		Session		* ses;
		try {
			 ses = new Session(originalname);
		} catch (std::bad_alloc & ) {
			return ;
		}
		assert(ses != NULL);
		m_sessions.insert(std::pair<std::string, Session*>(originalname, ses));
	}
}

DWORD		Manager::Execute(stringstream & reportData) {
	std::map<std::string, Session *>::iterator	itr;
	DWORD	color = BB_GREEN;
	DWORD	tmp;

	for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr) {
		if (m_centralized) {
			reportData << "[msgs:eventlog_" << (*itr).first << "]\n";
			(*itr).second->SetCentralized(true);
		}
		tmp = (*itr).second->Execute(reportData);
		if (tmp > color)
			color = tmp;
	}
	if (m_centralized == false) {
		tmp = AnalyzeLogFilesSize(reportData, m_checkFullLogFile);
		if (tmp > color)
			color = tmp;
	}
	return color;
}

DWORD		Manager::GetMatchCount() {
	std::map<std::string, Session *>::iterator	itr;
	DWORD	inc = 0;

	for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr) {
		inc += (*itr).second->GetMatchCount();
	}
	return inc;
}

DWORD		Manager::GetIgnoreCount() {
	std::map<std::string, Session *>::iterator	itr;
	DWORD	inc = 0;

	for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr) {
		inc += (*itr).second->GetIgnoreCount();
	}
	return inc;
}

DWORD		Manager::GetTotalCount() {
	std::map<std::string, Session *>::iterator	itr;
	DWORD	inc = 0;

	for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr) {
		inc += (*itr).second->GetTotalCount();
	}
	return inc;
}

void		Manager::InitCentralizedMode() {
	std::list< std::string >::iterator	 itr;

	FreeSessions();
	for (itr = m_logFileList.begin(); itr != m_logFileList.end(); ++itr) {
		Rule		rule;

		// default matching rule for centralized mode
		AddRule((*itr), rule);
	}
}

static DWORD		myGetFileSize(LPCSTR path) {
	HANDLE			h;
	DWORD			size;

	if ((h = CreateFile(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL)) == INVALID_HANDLE_VALUE) {
		return 0;
	}
	size = GetFileSize(h, NULL);
	CloseHandle(h);
	return size;
}


DWORD				Manager::AnalyzeLogFilesSize(std::stringstream & reportData, bool checking) {
	std::list< std::string >::iterator			logitr;
	string										key;
	HKEY 										hk; 
	TCHAR										val[REG_BUF_SIZE + 1];
	TCHAR										buf[REG_BUF_SIZE + 1];
	DWORD										color = BB_GREEN;

	reportData << format("\nEventLog Statistics:\n") << endl;
	for (logitr = m_logFileList.begin(); logitr != m_logFileList.end(); logitr++) {
		DWORD		size, type;
		string		tmp, logpath;
		DWORD		maxsize, actualsize, retention;

		reportData << format("- %s") % (*logitr) << endl;
		maxsize = actualsize = retention = size = type = 0;
		key = EVENT_LOG_REG_KEY + string("\\") + (*logitr);
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ | KEY_QUERY_VALUE , &hk)) {
			reportData << "error : can't get registry information" << endl;
			continue ;
		}
		SecureZeroMemory(val, sizeof(val));
		SecureZeroMemory(buf, sizeof(buf));
		size = REG_BUF_SIZE;
		type = REG_EXPAND_SZ;
		if (RegQueryValueEx(hk, "File", NULL, &type, (LPBYTE)val, &size) == ERROR_SUCCESS) {
			if (ExpandEnvironmentStrings(val, buf, REG_BUF_SIZE) != 0) {
				logpath = buf;
			}
		} else {
			reportData << "error : can't get eventlog file path" << endl;
		}
		size = sizeof(maxsize);
		type = REG_DWORD;
		RegQueryValueEx(hk, "MaxSize", NULL, &type, (LPBYTE)&maxsize, &size);
		size = sizeof(maxsize);
		type = REG_DWORD;
		actualsize = myGetFileSize(logpath.c_str());
		RegQueryValueEx(hk, "Retention", NULL, &type, (LPBYTE)&retention, &size);
		RegCloseKey(hk); 
		// get event log last event timestamp and number of events
		//
		HANDLE					h;
		EVENTLOGRECORD			*pevlr; 
		DWORD					dwRead, dwNeeded, numRecords;
		long					myNow;
		BYTE					bBuffer[EVENT_BUFFER_SIZE]; 

		dwRead = dwNeeded = numRecords = 0;
		myNow = Session::Now();
		h = OpenEventLog(NULL, (*logitr).c_str());   
		if (h)  {
			GetNumberOfEventLogRecords(h, &numRecords);
			pevlr = (EVENTLOGRECORD *) &bBuffer;
			if (ReadEventLog(h,                // event log handle 
				EVENTLOG_FORWARDS_READ |  // reads forward 
				EVENTLOG_SEQUENTIAL_READ, // sequential read 
				0,            // ignored for sequential reads 
				pevlr,        // pointer to buffer 
				EVENT_BUFFER_SIZE,  // size of buffer 
				&dwRead,      // number of bytes read 
				&dwNeeded) != 0) {
					pevlr = (EVENTLOGRECORD *) &bBuffer; 
			}
			CloseEventLog(h); 
		}
	
		DWORD days = (myNow - pevlr->TimeGenerated) / 86400;
		if (days == 0)	
			days = 1;
		reportData << format("  %u events since %u days (%.02f events/day)") % numRecords % days % (double)((double)numRecords / (double)days)<< endl;
		reportData << format("  current size is %u kb (max size is set to %u kb)") % (actualsize / 1024) % (maxsize / 1024) << endl;
		if (checking && retention == 0xffffffff && actualsize >= maxsize) {
			color = BB_RED;
			reportData << " &red this event log is full." << endl;
		}
		reportData << "  retention is set to : ";
		switch ( retention ) {
		case 0xffffffff :
			reportData << "Do not overwrite events (clear log manually)";
			break;
		case 0x0:
			reportData << "Overwrite events as needed";
			break;
		default:
			reportData << "Overwrite events older than " << (retention / 86400) << " days";
			break;
		}
		reportData << endl;
	}
	return color;
}

void		Manager::GetLogFileList() {
	HKEY 				hk; 

	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, EVENT_LOG_REG_KEY, 0, KEY_READ, &hk))
	{
		return ;
	}
	TCHAR		achKey[REG_BUF_SIZE + 1];   // buffer for subkey name
	DWORD		cbName;                   // size of name string 
	DWORD		cSubKeys = 0;               // number of subkeys 
	FILETIME	ftLastWriteTime;      // last write time 
	DWORD		retCode; 

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(
		hk,                    // key handle 
		NULL,  
		NULL,  
		NULL,  
		&cSubKeys,               // number of subkeys 
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		&ftLastWriteTime);       // last write time 
	if (cSubKeys){
		DWORD		inc;

		for (inc = 0; inc < cSubKeys; ++inc)  { 
			cbName = REG_BUF_SIZE;
			retCode = RegEnumKeyEx(hk, inc,
				achKey, 
				&cbName, 
				NULL, 
				NULL, 
				NULL, 
				&ftLastWriteTime); 
			if (retCode == ERROR_SUCCESS) {
				string		logfile = achKey;
				std::transform(logfile.begin(), logfile.end(), logfile.begin(), tolower);
				m_logFileList.push_back(logfile);
			}
		}
	} 
	RegCloseKey(hk); 
}
