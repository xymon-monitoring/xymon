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
// $Id: EventLog.h 79 2008-02-08 15:03:16Z sharpyy $


#ifndef __EVENTMANAGER_H__
#define __EVENTMANAGER_H__

#include <string>
#include <list>
#include <map>


#define EVENT_BUFFER_SIZE 1024*64

#define EVENT_LOG_REG_KEY	"SYSTEM\\CurrentControlSet\\Services\\Eventlog"
#define	REG_BUF_SIZE		1024
#define DESC_BUF_SIZE		1024
#define TIME_BUF			256
#define MAX_STRING_EVENT	24

#define MSG_ID_MASK 0x0000FFFF

#define ACCOUNT_SIZE	128
#define UNKOWN_ACCOUNT	"n/a"

namespace EventLog {

enum BBLevels { BB_GREEN, BB_YELLOW, BB_RED };

//
// Event Log Rule class
// 
class Rule {
	private :
		bool			m_useId; // true if event ID specified
		DWORD			m_id; // event ID
		std::string		m_source; // event source
		DWORD			m_alarmColor; // alarm color for the rules
		bool			m_ignore;  // is it an ignore rule
		WORD			m_type; // event type
		std::string		m_value; // value to match
		std::string		m_user; // user of the event
		DWORD			m_delay; // delay in seconds
		DWORD			m_count; // Maximum number of matches for the rules. Default is 0 which mean rule is checked all time
		DWORD			m_countTmp; // Current number of matches
		DWORD			m_priority; // priority of the rule. Default is 0

	public :
		Rule();
		~Rule();
		Rule(const Rule & rule);
		DWORD				GetEventId() const { return m_id; }
		void				SetEventId(DWORD eventId) { m_useId = true; m_id = eventId; }
		bool				GetIgnore() const { return m_ignore; } 
		void				SetIgnore(bool ignore) { m_ignore = ignore; } 
		const std::string & GetSource() const { return m_source; } 
		void				SetSource(const std::string & source);
		void				SetAlarmColor(DWORD alarm) { m_alarmColor = alarm; }
		DWORD				GetAlarmColor() const { return m_alarmColor; }
		void				SetType(WORD type);
		WORD				GetType() const { return m_type; }
		bool				SetType(const std::string & type);

		const std::string & GetValue() const { return m_value; }
		void				SetValue(const std::string & value);
		DWORD				GetDelay() const { return m_delay; }
		void				SetDelay(DWORD delay) { m_delay = delay; }
		DWORD				GetCount() const { return m_count; }
		void				SetCount(DWORD count) { m_count = count; }
		void				IncrementCurrentCount() { m_countTmp++; }
		DWORD				GetCurrentCount() const { return m_countTmp; }
		void				SetCurrentCount(DWORD currentCount) { m_countTmp = currentCount; }
		DWORD				GetPriority() const { return m_priority; }
		void				SetPriority(DWORD priority) { m_priority = priority; }
		const std::string & GetUser() const { return m_user; }
		void				SetUser(const std::string & user) { m_user = user; }
};

typedef std::multimap < std::string, HMODULE > 					eventlog_mod_t;
typedef eventlog_mod_t::iterator								eventlog_mod_itr_t;
typedef std::pair< eventlog_mod_itr_t, eventlog_mod_itr_t >		eventlog_mode_range_t;

//
//  Session class
//
class Session {
	private:
		eventlog_mod_t						m_sources; // countains the handle of the open file to avoid multiple loadlibrary
		std::list<Rule>						m_matchRules; // countains the match rules
		std::list<Rule>						m_ignoreRules; // countains the ignore rules
		DWORD								m_maxDelay;
		DWORD								m_now;
		std::string							m_logfile;
		bool								m_centralized;
		DWORD								m_maxData;

		// counters 
		DWORD								m_total;
		DWORD								m_match;
		DWORD								m_ignore;
		DWORD								m_numberOfRecords;
		DWORD								m_oldestRecord;

	private :
		void			FreeSources();
		DWORD			GetMaxDelay();
		DWORD			AnalyzeEvent(const EVENTLOGRECORD * ev, std::stringstream & reportData);
		bool			ApplyRule(const Rule & rule, const EVENTLOGRECORD * ev);
		void			GetEventDescription(const EVENTLOGRECORD * ev, std::string & description);
		void			GetEventUser(const EVENTLOGRECORD * ev, std::string & user);
		void			InitCounters();
		void			LoadEventMessageFile(const std::string & source, const EVENTLOGRECORD * ev);
		

	public :
		DWORD		GetMatchCount() { return m_match; }
		DWORD		GetIgnoreCount() { return m_ignore; }
		DWORD		GetTotalCount() { return m_total; }
		bool		GetCentralized() const { return m_centralized; }
		void		SetCentralized(bool centralized) { m_centralized = centralized; }
		void		SetMaxData(DWORD maxData) { m_maxData = maxData; }
		DWORD			Execute(std::stringstream & reportData);
		static LONG		Now();
		void			AddRule(const Rule & rule);

		Session(const std::string logfile);
		~Session();
};


//
//  Manager 
//
class Manager {
	private:
		std::map< std::string, Session * >			m_sessions;
		std::list< std::string >					m_logFileList;
		bool										m_checkFullLogFile;
		bool										m_centralized;

	private:
		DWORD				AnalyzeLogFilesSize(std::stringstream & reportData, bool checking);
		void				GetLogFileList();
		void				FreeSessions();

	public :
		void		AddRule(const std::string & logfile, const Rule & rule);
		void		AddLogFile(const std::string & logfile);
		void		SetMaxData(const std::string & logfile, DWORD maxData);
		DWORD		Execute(std::stringstream & reportData);
		DWORD		GetMatchCount();
		DWORD		GetIgnoreCount();
		DWORD		GetTotalCount();
		bool		GetCentralized() const { return m_centralized; }
		void		SetCentralized(bool centralized) { InitCentralizedMode(); m_centralized = centralized; }
		void		InitCentralizedMode();
		bool		GetCheckFullLogFile() const { return m_checkFullLogFile; }
		void		SetCheckFullLogFile(bool checkFullLogFile) { m_checkFullLogFile = checkFullLogFile; }
		Manager();
		~Manager();
};


}; // namespace EventLog

#endif // !__EVENTMANAGER_H__

