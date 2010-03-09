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
// $Id: filesystem.cpp 99 2008-06-04 15:44:52Z sharpyy $

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <list>
#include <io.h>
#include "utils.h"
#include "digest.h"
#include "filesystem.h"
#include "boost/format.hpp"
#include <boost/regex.hpp>

using namespace std;
using namespace boost;
using boost::format;

static const BBWinAgentInfo_t 		filesystemAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"filesystem",    					// agentName;
	"agent used to monitor files and directories",        // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE			// flags
};                

static bool		IsBackQuotedString(const std::string & str) {
	if (str.substr(0, 1) == "`" && str.substr(str.size() - 1, 1) == "`")
		return true;
	return false;
}

// execute backquoted commands
void		AgentFileSystem::GetLinesFromCommand(const std::string & command, std::list<string> & list) {
	utils::ProcInOut		process;
	string					cmd, out;

	// remove backquoted from command
	cmd = command.substr(1, command.size() - 2);
	if (process.Exec(cmd, out, MAX_TIME_BACKQUOTED_COMMAND)) {
		// remove '\r; 
		out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
		std::string::size_type pos = 0;
		if (out.size() == 0)
			return ;
		for ( ;pos < out.size(); ) {
			std::string::size_type	next = out.find_first_of("\n", pos);
			string					line;
			if (next > 0 && next < out.size()) {
				line = out.substr(pos, (next - pos));
				if (line != "")
					list.push_back(line);
				pos += (next - pos) + 1;
			} else {
				line = out.substr(pos, out.size() - pos);
				if (line != "")
					list.push_back(line);
				pos = out.size();
			}
			m_mgr.Log(LOGLEVEL_DEBUG, "find line %s", line.c_str());
		}
	} else {
		string			err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_WARN, "%s command failed : %s", cmd.c_str(), err.c_str());
	}
}

bool		AgentFileSystem::InitCentralMode() {
	string clientLocalCfgPath = m_mgr.GetSetting("tmppath") + (string)"\\clientlocal.cfg";

	m_mgr.Log(LOGLEVEL_DEBUG, "start %s", __FUNCTION__);
	// clear existing rules. We parse client-local.cfg each run time
	m_files.clear();
	m_dirs.clear();
	m_logfiles.clear();
	m_linecounts.clear();
	m_mgr.Log(LOGLEVEL_DEBUG, "checking file %s", clientLocalCfgPath.c_str());
	ifstream		conf(clientLocalCfgPath.c_str());

	if (!conf) {
		string	err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_INFO, "can't open %s : %s", clientLocalCfgPath.c_str(), err.c_str());
		return false;
	}
	m_mgr.Log(LOGLEVEL_DEBUG, "reading file %s", clientLocalCfgPath.c_str());
	string			buf;
	bool			skipNextLineFlag = false;
	while (!conf.eof()) {
		string			value;

		if (skipNextLineFlag == true) {
			skipNextLineFlag = false;
		} else {
			utils::GetConfigLine(conf, buf);
		}
		if (utils::parseStrGetNext(buf, "file:", value)) {
			fs_file_t		file;
			string			hash;
			
			file.hashtype = NONE;
			file.logfile = false;
			// substr(3) : we do not parse C:\ to get the next part to ':' separator
			if (utils::parseStrGetLast(value.substr(3), ":", hash)) {
				value.erase(value.end() - (hash.size() + 1), value.end());
				if (hash == "md5") {
					m_mgr.Log(LOGLEVEL_DEBUG, "will use md5 hash type", hash.c_str());
					file.hashtype = MD5;
				} else if (hash == "sha1") {
					m_mgr.Log(LOGLEVEL_DEBUG, "will use sha1 hash type", hash.c_str());
					file.hashtype = SHA1;
				} else {
					m_mgr.Log(LOGLEVEL_WARN, "Unknow hash type for file configuration : %s", hash.c_str());
				}
			}
			if (IsBackQuotedString(value)) {
				std::list<string>			list;
				std::list<string>::iterator	itr;

				GetLinesFromCommand(value, list);
				for (itr = list.begin(); itr != list.end(); ++itr) {
					file.path = *itr;
					m_mgr.Log(LOGLEVEL_DEBUG, "will check file %s", file.path.c_str());
					m_files.push_back(file);
				}
			} else {
				file.path = value;
				m_mgr.Log(LOGLEVEL_DEBUG, "will check file %s", file.path.c_str());
				m_files.push_back(file);
			}
		} else if (utils::parseStrGetNext(buf, "dir:", value)) {
			if (IsBackQuotedString(value)) {
				std::list<string>			list;
				std::list<string>::iterator	itr;

				GetLinesFromCommand(value, list);
				for (itr = list.begin(); itr != list.end(); ++itr) {
					m_mgr.Log(LOGLEVEL_DEBUG, "will check directory %s", (*itr).c_str());
					m_dirs.push_back(*itr);
				}
			} else {
				m_mgr.Log(LOGLEVEL_DEBUG, "will check directory %s", value.c_str());
				m_dirs.push_back(value);
			}
		} else if (utils::parseStrGetNext(buf, "log:", value)) {
			fs_logfile_t	logfile;
			string			sizeStr;
			
			if (utils::parseStrGetLast(value.substr(3), ":", sizeStr)) {
				value.erase(value.end() - (sizeStr.size() + 1), value.end());
				std::istringstream iss(sizeStr);
				iss >> logfile.maxdata;
				m_mgr.Log(LOGLEVEL_DEBUG, "will use maxdata size : %u", logfile.maxdata);
			} else {
				continue ;
			}
			// read next arguments : ignore and trigger options
			while (!conf.eof()) {
				string			arg;

				utils::GetConfigLine(conf, buf);
				if (utils::parseStrGetNext(buf, "ignore ", arg)) {
					m_mgr.Log(LOGLEVEL_DEBUG, "will ignore : %s", arg.c_str());
					logfile.ignores.push_back(arg);
				} else if (utils::parseStrGetNext(buf, "trigger ", arg)) {
					m_mgr.Log(LOGLEVEL_DEBUG, "will trigger : %s", arg.c_str());
					logfile.triggers.push_back(arg);
				} else {
					skipNextLineFlag = true;
					break ;
				}
			}
			if (IsBackQuotedString(value)) {
				std::list<string>			list;
				std::list<string>::iterator	itr;

				GetLinesFromCommand(value, list);
				for (itr = list.begin(); itr != list.end(); ++itr) {
					logfile.path = (*itr);
					m_logfiles.push_back(logfile);
				}
			} else {
				logfile.path = value;
				m_logfiles.push_back(logfile);
			}
		}  else if (utils::parseStrGetNext(buf, "linecount:", value)) {
			fs_linecount_t	linecount;
			
			// read next arguments : 
			while (!conf.eof()) {
				fs_count_t		count;

				utils::GetConfigLine(conf, buf);
				if (strncmp(buf.c_str(), "file:", 5) != 0 && 
					strncmp(buf.c_str(), "log:", 4) != 0 && 
					strncmp(buf.c_str(), "linecount:", 10) != 0 && 
					strncmp(buf.c_str(), "dir:", 4) != 0 && strchr(buf.c_str(), ' ') != NULL) {
						size_t	res = buf.find(" ");

						count.count = 0;
						count.keyword = buf.substr(0, res);
						count.pattern = buf.substr(res + 1);
						m_mgr.Log(LOGLEVEL_DEBUG, "will linecount for keyword %s with pattern %s", 
							count.keyword.c_str(), count.pattern.c_str());
						linecount.counts.push_back(count);
				} else {
					skipNextLineFlag = true;
					break ;
				}
			}
			if (IsBackQuotedString(value)) {
				std::list<string>			list;
				std::list<string>::iterator	itr;

				GetLinesFromCommand(value, list);
				for (itr = list.begin(); itr != list.end(); ++itr) {
					linecount.path = (*itr);
					m_linecounts.push_back(linecount);
				}
			} else {
				linecount.path = value;
				m_linecounts.push_back(linecount);
			}
		} 

	}
	return false;
}

bool		AgentFileSystem::GetTimeString(const FILETIME & filetime, string & output) {
	__int64	ftime = (filetime.dwHighDateTime * MAXDWORD) + filetime.dwLowDateTime;
	SYSTEMTIME	fsystime;
	FILETIME	floctime;
	time_t		epoch;

	if (::FileTimeToLocalFileTime(&filetime, &floctime) == 0) {
		return false;
	}
	if (::FileTimeToSystemTime(&floctime, &fsystime) == 0) {
		return false;
	}
	utils::SystemTimeToTime_t(&fsystime, &epoch);
	stringstream reportData;
	reportData << format("%llu (%02d/%02d/%d-%02d:%02d:%02d)") 
					% (unsigned __int64)epoch
					% fsystime.wYear 
					% fsystime.wMonth
					% fsystime.wDay
					% fsystime.wHour
					% fsystime.wMinute
					% fsystime.wSecond;
	output = reportData.str();
	return true;
}

bool		AgentFileSystem::GetFileAttributes(const string & path, stringstream & reportData, bool logfile) {
	HANDLE							handle;
	BY_HANDLE_FILE_INFORMATION		handle_file_info;
	__int64							fsize;   

	FILE *f = _fsopen(path.c_str(), "rt", _SH_DENYNO);
	if (f == NULL) {
		string		err;

		utils::GetLastErrorString(err);
		reportData << "ERROR: " << err << endl;
		return false;
	}
	if ((handle = (HANDLE)_get_osfhandle(f->_file)) == INVALID_HANDLE_VALUE) {
		string		err;

		utils::GetLastErrorString(err);
		reportData << "ERROR: " << err << endl;
		return false;
	}
	if (::GetFileInformationByHandle(handle, &handle_file_info) == 0) {
		string		err;

		utils::GetLastErrorString(err);
		reportData << "ERROR: " << err << endl;
		fclose(f);
		return false;
	}
	fsize = (handle_file_info.nFileSizeHigh * MAXDWORD) + handle_file_info.nFileSizeLow;
	// we get size information for logfile parsing
	if (logfile == true) {
		std::map<std::string, fs_logfile_seekdata_t>::iterator		res = m_seekdata.find(path);
		if (res == m_seekdata.end()) { 
			fs_logfile_seekdata_t		seekdata;
			SecureZeroMemory(&seekdata, sizeof(seekdata));
			seekdata.used = true;
			seekdata.size = fsize;
			m_seekdata.insert(std::pair<std::string, fs_logfile_seekdata_t> (path, seekdata));
		} else {
			// the seek data is used
			fs_logfile_seekdata_t & seekdata = (*res).second;
			seekdata.used = true;
			seekdata.size = fsize;
		}
	}
	reportData << format("type:0x%05x ") % handle_file_info.dwFileAttributes;
	if (handle_file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		reportData << "(dir)" << endl;
	else
		reportData << "(file)" << endl;
	reportData << "mode:777 (not implemented)" << endl;
	reportData << format("linkcount:%u") % handle_file_info.nNumberOfLinks << endl;
	reportData << "owner:0 (not implemented)" << endl;
	reportData << "group:0 (not implemented)" << endl;
	reportData << format("size:%lu") % fsize << endl;
	string output;
	GetTimeString(handle_file_info.ftLastAccessTime, output);
	reportData << "atime:" << output << endl;
	GetTimeString(handle_file_info.ftCreationTime, output);
	reportData << "ctime:" << output << endl;
	GetTimeString(handle_file_info.ftLastWriteTime, output);
	reportData << "mtime:" << output << endl;
	fclose(f);
	return true;
}

bool	AgentFileSystem::ListFiles(const std::string & path, std::stringstream & report, __int64 & size) {
	WIN32_FIND_DATA		find_data;
	string				mypath = path + "\\*";
	
	HANDLE handle = FindFirstFile(mypath.c_str(), &find_data);
	if (handle != INVALID_HANDLE_VALUE) {
		 // skip "." and ".." directories
		FindNextFile(handle, &find_data);
		while (FindNextFile(handle, &find_data)) {
			string	newpath = path + "\\" + find_data.cFileName;
			__int64 tmpsize = ((find_data.nFileSizeHigh * MAXDWORD) + find_data.nFileSizeLow);
			report << format("%lu\t %s") % ((tmpsize < 1024 && tmpsize != 0) ? 1 : tmpsize / 1024) % newpath << endl;
			size += tmpsize;
			if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				ListFiles(newpath , report, size);
			}
		}
		FindClose(handle);
	} else {
		string		err;

		utils::GetLastErrorString(err);
		report << "ERROR: " << err << endl;
		return false;
	}
	return true;
}

bool		AgentFileSystem::ExecuteDirRule(const std::string & dir) {
	stringstream 		reportData;	
	__int64				size = 0;
	bool				ret;

	string title = "dir:" + dir;
	ret = ListFiles(dir, reportData, size);
	if (reportData.str().substr(0, 5) != "ERROR")
		reportData << format("%lu\t %s") % ((size < 1024 && size != 0) ? 1 : size / 1024) % dir << endl;
	reportData << endl;
	m_mgr.ClientData(title.c_str(), reportData.str().c_str());
	return ret;
}

DWORD		AgentFileSystem::ApplyRulesOnLine(fs_logfile_t & logfile, std::string line) {
	std::list<string>::iterator		itr;
	
	// triggers first 
	for (itr = logfile.triggers.begin(); itr != logfile.triggers.end(); ++itr) {
		boost::regex e((*itr), boost::regbase::perl);
		boost::match_results<std::string::const_iterator>	what;
		if(boost::regex_search(line, what, e) != 0)	{
			return MATCH_TRIGGER;
		}
	}
	// ignore
	for (itr = logfile.ignores.begin(); itr != logfile.ignores.end(); ++itr) {
		boost::regex e((*itr), boost::regbase::perl);
		boost::match_results<std::string::const_iterator>	what;
		if(boost::regex_search(line, what, e) != 0)	{
			return MATCH_IGNORE;
		}
	}
	return MATCH_NONE;
}

bool		AgentFileSystem::ExecuteLogFileRule(fs_logfile_t & logfile) {
	std::map<std::string, fs_logfile_seekdata_t>::iterator	res;
	stringstream						reportData;

	// first exec file rule
	fs_file_t		file;
	file.hashtype = NONE;
	file.path = logfile.path;
	file.logfile = true;
	if (ExecuteFileRule(file) == false) {
		// first step failed
		return false;
	}
	// get or create the seek data
	res = m_seekdata.find(logfile.path);
	FILE *f = _fsopen(logfile.path.c_str(), "rt", _SH_DENYNO);
	if (f == NULL) {
		return false;
	}
	fs_logfile_seekdata_t & seekdata = (*res).second;
	seekdata.used = true;
	TCHAR			buf[LOGFILE_BUFFER];
	fpos_t			pos = 0;

	if (seekdata.point[SEEKDATA_START_POINT] > 0) {
		// if file has been rotated, we start from 0
		if (seekdata.size < seekdata.point[SEEKDATA_START_POINT]) {
			SecureZeroMemory(seekdata.point, sizeof(seekdata.point));
		} else { // get the save point
			fseek(f, (long)seekdata.point[SEEKDATA_START_POINT], SEEK_SET);
		}
	} 
	// skip to end of file depending file size and logfile maxdata
	if (seekdata.point[SEEKDATA_START_POINT] == 0 && seekdata.size > logfile.maxdata) {
		if (fseek(f, (long)(seekdata.size - logfile.maxdata), SEEK_SET)) {
			// failed to change position
			SecureZeroMemory(seekdata.point, sizeof(seekdata.point));
		} else { // we skip data
			reportData << SKIP_STRING << endl;
		}
	}
	DWORD				SkipInTheMiddle = 0;
	bool				joinLines = false;
	string				line;
	while ((fgets(buf, LOGFILE_BUFFER, f) != NULL)) {
		stringstream			tmp;

		size_t res = strlen(buf);
		// join lines too large for the fixed size buffer
		if (joinLines == true)
			line += buf;
		else
			line = buf;
		if (res > 1 && buf[res - 1] != '\n') {
			joinLines = true;
		} else {
			joinLines = false;
		}
		// Ignore or trigger line
		if (line.size() > 0 && joinLines == false) {
			DWORD res = ApplyRulesOnLine(logfile, line);
			if (res == MATCH_NONE && reportData.str().size() > logfile.maxdata) {
				if (SkipInTheMiddle++ == 0)
					reportData << SKIP_STRING << endl;
			} else if (res == MATCH_NONE || res == MATCH_TRIGGER) {
				if (SkipInTheMiddle > 0) 
					SkipInTheMiddle = 0;
				reportData << line;
			}
		}
	}
	if (fgetpos(f, &pos) != 0) {
		string	err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_WARN, "error on fgetpos for logfile %s : %s", logfile.path.c_str(), err.c_str());
	}
	// skip oldest value to save current position
	for (DWORD count = SEEKDATA_START_POINT; count > 0; --count) {
		seekdata.point[count] = seekdata.point[count - 1];
	}
	seekdata.point[0] = pos;
	fclose(f);
	// prepare the title of the section
	string title = "msgs:" + logfile.path;
	reportData << endl;
	string report = reportData.str();
	report.erase(std::remove(report.begin(), report.end(), '\r'), report.end());
	m_mgr.ClientData(title.c_str(), report.c_str());
	return true;
}

// load seek data from the logfetch.status
void					AgentFileSystem::LoadSeekData() {
	string seekdataCfgPath = m_mgr.GetSetting("tmppath") + (string)"\\logfetch.status";
	
	ifstream			ifstr(seekdataCfgPath.c_str());
	if (!ifstr)  {
		string		err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_INFO, "failed to save logfetch status %s : %s", seekdataCfgPath.c_str(), err.c_str());
		return ;
	}
	while (!ifstr.eof()) {
		fs_logfile_seekdata_t		data;

		SecureZeroMemory(&data, sizeof(data));
		string			buf;
		std::getline(ifstr, buf);
		for (DWORD count = SEEKDATA_MAX_POINT; count >= 0; --count) {
			size_t	res = buf.find_last_of(":");
			if (count != 0 && res > 0 && res < buf.size()) {

				string value = buf.substr(res + 1, buf.size() - res);
				std::istringstream iss(value);
				fpos_t pos = 0;
				iss >> pos;
				data.point[count - 1] = pos;
				data.used = false;
			} else if (count == 0) {
				m_mgr.Log(LOGLEVEL_DEBUG, "loading seekdata for file %s", buf.c_str());
				m_seekdata.insert(std::pair<std::string, fs_logfile_seekdata_t> (buf, data));
			} else {
				break  ;
			}
			buf.erase(res, buf.size() - res);
		}
	}
	ifstr.close();
}

// save seek data from the logfetch.status
void					AgentFileSystem::SaveSeekData() {
	string seekdataCfgPath = m_mgr.GetSetting("tmppath") + (string)"\\logfetch.status";
	std::map<std::string, fs_logfile_seekdata_t>::iterator	itr;

	if (m_seekdata.size() == 0)
		return ;
	ofstream			ofstr(seekdataCfgPath.c_str());
	if (!ofstr)  {
		string		err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_INFO, "failed to save logfetch status %s : %s", seekdataCfgPath.c_str(), err.c_str());
		return ;
	}
	for (itr = m_seekdata.begin(); itr != m_seekdata.end(); ++itr) {
		fs_logfile_seekdata_t & seekdata = (*itr).second;
		ofstr << (*itr).first.c_str();
		for (int count = 0; count < 7; ++count) {
			ofstr << ":" << seekdata.point[count];
		}
		ofstr << endl;
	}
	ofstr.close();
}

void		AgentFileSystem::ExecuteLineCountRule(fs_linecount_t & linecount) {
	stringstream	reportData;

	FILE *f = _fsopen(linecount.path.c_str(), "rt", _SH_DENYNO);
	if (f == NULL) {
		string		err;

		utils::GetLastErrorString(err);
		reportData << "ERROR: " << err << endl;
	} else {
		TCHAR				buf[LOGFILE_BUFFER];
		fpos_t				pos = 0;
		bool				joinLines = false;
		string				line;
		while ((fgets(buf, LOGFILE_BUFFER, f) != NULL)) {
			stringstream			tmp;

			size_t res = strlen(buf);
			// join lines too large for the fixed size buffer
			if (joinLines == true)
				line += buf;
			else
				line = buf;
			if (res > 1 && buf[res - 1] != '\n')
				joinLines = true;
			else
				joinLines = false;
			if (line.size() > 0 && joinLines == false) {
				for (std::list<fs_count_t>::iterator itr = linecount.counts.begin(); 
					itr != linecount.counts.end(); ++itr) {
						boost::regex e((*itr).pattern, boost::regbase::perl);
						boost::match_results<std::string::const_iterator>	what;
						if(boost::regex_search(line, what, e) != 0)	{
							(*itr).count++;
						}
				}
			}
		}
		fclose(f);
	}
	linecount.path.erase(std::remove(linecount.path.begin(), linecount.path.end(), ':'), linecount.path.end());
	string title = "linecount:" + linecount.path;
	for (std::list<fs_count_t>::iterator itr = linecount.counts.begin(); 
		itr != linecount.counts.end(); ++itr) {
			reportData << (*itr).keyword << ": " << (*itr).count << endl;
	}
	m_mgr.ClientData(title.c_str(), reportData.str().c_str());
}

bool		AgentFileSystem::ExecuteFileRule(const fs_file_t & file) {
	stringstream 		reportData;	
	string				title;
	bool				ret;

	if (file.logfile == true) {
		title = "logfile:" + file.path;
	} else {
		title = "file:" + file.path;
	}
	if ((ret = this->GetFileAttributes(file.path, reportData, file.logfile)) && file.hashtype != NONE) {
		digestctx_t			*dig;
		HANDLE				hFile;
		string				digstr;
		char				buf[1024];
		DWORD				read;

		if (file.hashtype == MD5) {
			dig = digest_init("md5");
		} else {
			dig = digest_init("sha1");
		}
		if (dig != NULL) {
			LPTSTR		tmp;
			hFile = CreateFile(file.path.c_str(),     // file to open
				GENERIC_READ,         // open for reading
				NULL, // do not share
				NULL,                   // default security
				OPEN_EXISTING, // default flags
				0,   // asynchronous I/O
				0);                // no attr. template

			if (hFile != INVALID_HANDLE_VALUE) {
				while (ReadFile(hFile, buf, 1024, &read, NULL)) {
					if (read == 0)
						break ;
					digest_data(dig, (unsigned char *)buf, read);
				}
				CloseHandle(hFile);
				tmp = digest_done(dig);
				digstr = tmp;
				if (tmp) free(tmp);
				reportData << digstr << endl;
			}
		}
	}
	reportData << endl;
	m_mgr.ClientData(title.c_str(), reportData.str().c_str());
	return ret;
}

void		AgentFileSystem::ExecuteRules() {
	for (std::list<fs_file_t>::iterator file_itr = m_files.begin(); file_itr != m_files.end(); ++file_itr) {
		ExecuteFileRule(*file_itr);
	}
	for (std::list<string>::iterator dir_itr = m_dirs.begin(); dir_itr != m_dirs.end(); ++ dir_itr) {
		ExecuteDirRule(*dir_itr);
	}
	for (std::list<fs_logfile_t>::iterator logfile_itr = m_logfiles.begin(); logfile_itr != m_logfiles.end(); ++logfile_itr) {
		ExecuteLogFileRule(*logfile_itr);
	}
	for (std::list<fs_linecount_t>::iterator linecount_itr = m_linecounts.begin(); linecount_itr != m_linecounts.end(); ++linecount_itr) {
		ExecuteLineCountRule(*linecount_itr);
	}
	// clean unused seekdata 
	// may be files are not monitored anymore
	std::map<std::string, fs_logfile_seekdata_t>::iterator	itr;
	for (itr = m_seekdata.begin(); itr != m_seekdata.end(); ) {
		if ((*itr).second.used == false) {
			m_seekdata.erase(itr++);
		} else {
			(*itr).second.used = false;
			++itr;
		}
	}
}

void 		AgentFileSystem::Run() {
	stringstream 		reportData;	
	if (m_mgr.IsCentralModeEnabled() == false)
		return ;
	InitCentralMode();
	ExecuteRules();
}

bool AgentFileSystem::Init() {
	if (m_mgr.IsCentralModeEnabled() == false) {
		return true;
	}
	LoadSeekData();
	return true;
}

AgentFileSystem::AgentFileSystem(IBBWinAgentManager & mgr) : m_mgr(mgr) {
}

AgentFileSystem::~AgentFileSystem() {
	SaveSeekData();
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentFileSystem(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &filesystemAgentInfo;
}
