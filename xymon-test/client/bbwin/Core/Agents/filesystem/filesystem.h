//this file is part of BBWin
//Copyright (C)2008 Etienne GRIGNON  ( etienne.grignon@gmail.com )
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
// $Id: filesystem.h 77 2008-02-07 16:05:27Z sharpyy $

#ifndef		__FILESYSTEM_H__
#define		__FILESYSTEM_H__

#include "IBBWinAgent.h"

#define MAX_TIME_BACKQUOTED_COMMAND		8000
#define LOG_MAXDATA						10240
#define SEEKDATA_MAX_POINT				7
#define SEEKDATA_START_POINT			6
#define SEEKDATA_END_POINT				0

#define LOGFILE_BUFFER					64

#define MATCH_NONE						0
#define MATCH_TRIGGER					1
#define MATCH_IGNORE					2

#define SKIP_STRING "<...SKIPPED...>"

enum hash_type_t { NONE, MD5, SHA1 };


// Struct used to save seek points 
typedef struct				fs_logfile_seekdata_s {
	fpos_t					point[SEEKDATA_MAX_POINT];
	bool					used;	// tell if the logfile is still monitored 
									//and that seek data are still necessary
	__int64					size;	// current size
}							fs_logfile_seekdata_t;

// Struct used for log file monitoring
typedef struct				fs_logfile_s {
	std::string				path;
	DWORD					maxdata;
	std::list<std::string>	ignores;
	std::list<std::string>	triggers;
}							fs_logfile_t;

// Struct used for linecount monitoring
typedef struct				fs_count_s {
	std::string				keyword;
	std::string				pattern;
	DWORD					count;
}							fs_count_t;

// Struct used for linecount monitoring
typedef struct				fs_linecount_s {
	std::string				path;
	std::list<fs_count_t>	counts;
}							fs_linecount_t;

// Struct used for file monitoring
typedef struct			fs_file_s {
	std::string			path;
	enum hash_type_t	hashtype;
	bool				logfile; // true if it is a logfile
}						fs_file_t;

class AgentFileSystem : public IBBWinAgent
{
	private :
		std::list<fs_file_t>							m_files;
		std::list<std::string>							m_dirs;
		std::list<fs_logfile_t>							m_logfiles;
		std::map<std::string, fs_logfile_seekdata_t>	m_seekdata;
		std::list<fs_linecount_t>						m_linecounts;

	private :
		IBBWinAgentManager 		& m_mgr;
		std::string				m_testName;
		bool					InitCentralMode();
		void					ExecuteRules();

		bool					ExecuteFileRule(const fs_file_t & file);
		bool					GetFileAttributes(const std::string & path, std::stringstream & reportData, bool logfile);
		bool					GetTimeString(const FILETIME & ftime, std::string & output);
		bool					ExecuteDirRule(const std::string & dir);
		bool					ListFiles(const std::string & path, std::stringstream & report, __int64 & size);
		void					GetLinesFromCommand(const std::string & command, std::list<std::string> & list);
		bool					ExecuteLogFileRule(fs_logfile_t & logfile);
		DWORD					ApplyRulesOnLine(fs_logfile_t & logfile, std::string line);
		void					ExecuteLineCountRule(fs_linecount_t & linecount);
		
		void					LoadSeekData();
		void					SaveSeekData();


	public :
		AgentFileSystem(IBBWinAgentManager & mgr) ;
		~AgentFileSystem();
		bool Init();
		void Run();
};


#endif 	// !__FILESYSTEM_H__

