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
// $Id: Logging.cpp 96 2008-05-21 17:30:49Z sharpyy $

#include <windows.h>
#include <time.h>
#include <stdarg.h>

#include <string>
#include <iostream>
#include <fstream>
using namespace std;

#include "BBWinService.h"

#include "logging.h"

CRITICAL_SECTION  		m_logCriticalSection;
CRITICAL_SECTION  		m_eventCriticalSection;

Logging::Logging() {
	InitializeCriticalSection(&m_logCriticalSection);
	InitializeCriticalSection(&m_eventCriticalSection);
	m_logLevel = LOGLEVEL_DEFAULT;
	m_fileHandle = NULL;
}

void Logging::setFileName(const std::string & fileName) {
	m_fileName = fileName;
}


void Logging::open() {
	if ((m_fileHandle = fopen(m_fileName.c_str(), "a")) == NULL) {
		throw LoggingException("Failed to create log file");
	}
}

void Logging::close() {
	if (m_fileHandle != NULL) fclose(m_fileHandle);
}

Logging::~Logging() {
	DeleteCriticalSection(&m_logCriticalSection);
	DeleteCriticalSection(&m_eventCriticalSection);
}

#define TIME_BUF			256
void Logging::write(LPCTSTR log, va_list ap) {
	struct _SYSTEMTIME  ts;
	string				time, date;
	TCHAR				timebuf[TIME_BUF + 1];
	
	EnterCriticalSection(&m_logCriticalSection); 
	SecureZeroMemory(timebuf, sizeof(timebuf));
    GetLocalTime(&ts);
	try {
		open();
	} catch (LoggingException ex) {
		LeaveCriticalSection(&m_logCriticalSection);
		return ;
	}
	if (GetTimeFormat(LOCALE_SYSTEM_DEFAULT, TIME_FORCE24HOURFORMAT, &ts, "hh':'mm':'ss", timebuf, TIME_BUF) != 0) {
		time = timebuf;
	} else {
		time = "00:00:00";
	}
	if (GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, &ts, "yyyy/MM/dd", timebuf, TIME_BUF) != 0) {
		date = timebuf;
	} else {
		date = "unkwown";
	}
	fprintf(m_fileHandle, "%s %s ", date.c_str(), time.c_str());
	vfprintf(m_fileHandle, log, ap);
	fprintf(m_fileHandle, "\n");
	close();
    LeaveCriticalSection(&m_logCriticalSection);
}

void Logging::log(const int level, LPCTSTR log, ...) {
	va_list ap;
	va_start( ap, log );
	vlog(level, log, ap);
	va_end(ap);
}

void Logging::vlog(const int level, LPCTSTR log, va_list ap) {
	if (level <= m_logLevel) {
		string	_log;

		switch (level) {
			case LOGLEVEL_INFO :
				_log = "[INFO]: ";
				break ;
			case LOGLEVEL_ERROR :
				_log = "[ERROR]: ";
				break ;
			case LOGLEVEL_WARN :
				_log = "[WARN]: ";
				break ;
			case LOGLEVEL_DEBUG :
				_log = "[DEBUG]: ";
				break ;
			default :
				_log = "[UNKOWN]: ";
		}
		_log += log;
		write(_log.c_str(), ap);
	}
}

void Logging::reportEvent(WORD type, WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str) {
	HANDLE		hEvent;
	
	EnterCriticalSection(&m_eventCriticalSection); 
	if ((hEvent = RegisterEventSource(NULL, SZEVENTLOGNAME)) == NULL) {
		LeaveCriticalSection(&m_eventCriticalSection);
		return ;
	}
	ReportEvent(hEvent, type, category, eventId, NULL, nbStr, 0, str, NULL);
	DeregisterEventSource(hEvent);
	LeaveCriticalSection(&m_eventCriticalSection);
}

void Logging::reportInfoEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str) {
	reportEvent(EVENTLOG_INFORMATION_TYPE, category, eventId, nbStr, str);
}

void Logging::reportSuccessEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str) {
	reportEvent(EVENTLOG_SUCCESS, category, eventId, nbStr, str);
}

void Logging::reportErrorEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str) {
	reportEvent(EVENTLOG_ERROR_TYPE, category, eventId, nbStr, str);
}

void Logging::reportWarnEvent(WORD category, DWORD eventId, WORD nbStr, LPCTSTR *str) {
	reportEvent(EVENTLOG_WARNING_TYPE, category, eventId, nbStr, str);
}

void Logging::setLogLevel(DWORD level) {
	m_logLevel = level;
}

// LoggingException
LoggingException::LoggingException(const char* m) {
	msg = m;
}

string LoggingException::getMessage() const {
	return msg;
}
