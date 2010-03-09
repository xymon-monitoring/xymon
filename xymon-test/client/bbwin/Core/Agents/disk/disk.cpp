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
// $Id: disk.cpp 86 2008-02-13 13:53:54Z sharpyy $

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <sstream>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <list>
#include <map>
#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/format.hpp"
#include "disk.h"
#include "utils.h"

using namespace std;
using boost::format;
using namespace boost::posix_time;
using namespace boost::gregorian;

static const char *bbcolors[] = { "green", "yellow", "red", NULL };

static const BBWinAgentInfo_t 		diskAgentInfo =
{
	BBWIN_AGENT_VERSION,				// bbwinVersion;
	"disk",    							// agentName;
	"disk agent : report disk usage",	// agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE	// flags
};                

static const disk_unit_t		disk_unit_table[] = 
{
	{"kb", 1024},
	{"mb", 1048576},
	{"gb", 1073741824},
	{"tb", 1099511627776},
	{"kb", 1024},
	{"m", 1048576},
	{"g", 1073741824},
	{"t", 1099511627776},
	{NULL, 0},
};

static const disk_type_t		disk_type_table[] = 
{
	{"UNKOWN", DRIVE_UNKNOWN},
	{"FIXED", DRIVE_FIXED},
	{"REMOTE", DRIVE_REMOTE},
	{"CDROM", DRIVE_CDROM},
	{"RAMDISK", DRIVE_RAMDISK},
	{NULL, 0},
};

static const char * GetDiskTypeStr(DWORD type) {
	DWORD		inc;

	for (inc = 0; disk_type_table[inc].name; ++inc) {
		if (disk_type_table[inc].type == type)
			return disk_type_table[inc].name;
	}
	return disk_type_table[0].name;
}

bool			AgentDisk::GetSizeValue(const string & level, __int64 & val) {
	size_t		res;

	for (DWORD inc = 0; disk_unit_table[inc].name; ++inc) {
		res = level.find(disk_unit_table[inc].name);
		if (res > 0 && res < level.size()) {
			val = m_mgr.GetNbr(level.c_str()) * disk_unit_table[inc].size;
			return true;
		}
	}
	return false;
}

void			AgentDisk::GetMountPointData(LPTSTR driveName) {
	if (m_findFirstVolumeMountPoint == NULL || m_findNextVolumeMountPoint == NULL || m_findVolumeMountPointClose == NULL) 
		return;
	TCHAR 			mountPoint[BUFFER_SIZE + 1];
	HANDLE			h;
	disk_t			*disk;
	DWORD			driveType;
	BOOL			ret = 1, fResult;

	for (h = m_findFirstVolumeMountPoint(driveName, mountPoint, BUFFER_SIZE); 
		h != INVALID_HANDLE_VALUE && ret != 0; 
		ret = m_findNextVolumeMountPoint(h, mountPoint, BUFFER_SIZE)) {
		string		fullMountPoint = string(driveName) + string("\\") + string(mountPoint);
		driveType = GetDriveType(fullMountPoint.c_str());
		if (driveType != DRIVE_REMOVABLE) { // no floppy 
			try {
				disk = new disk_t;
			} catch (std::bad_alloc ex) {
				continue ;
			}
			if (disk == NULL)
				continue ;
			ZeroMemory(disk, sizeof(*disk));
			disk->type = driveType;
			m_mgr.Log(LOGLEVEL_DEBUG, "found mount point : %s", fullMountPoint.c_str());
			std::string::size_type	end = fullMountPoint.find_last_of("\\", fullMountPoint.size() - 1);
			std::string::size_type	begin = fullMountPoint.find_last_of("\\", end - 1);
			string dir = fullMountPoint.substr(begin + 1, end - begin - 1);
			m_mgr.Log(LOGLEVEL_DEBUG, "will use mount point shortname : %s", dir.c_str());
			strncpy(disk->letter, dir.c_str(), 7);
			disk->letter[8] = '\0';
			fResult = GetDiskFreeSpaceEx(fullMountPoint.c_str(), (PULARGE_INTEGER)&(disk->i64FreeBytesToCaller), 
				(PULARGE_INTEGER)&(disk->i64TotalBytes), (PULARGE_INTEGER)&(disk->i64FreeBytes));
			if (fResult != 0) {
				GetVolumeInformation(fullMountPoint.c_str(), disk->volumeName, MAXNAMELEN, &(disk->volumeSerialNumber), 
				 &(disk->maximumComponentLength), &(disk->fileSystemFlags), disk->fileSystemName, MAXNAMELEN);
			}
			if (disk->i64TotalBytes > 0)
				disk->percent = (DWORD)(((disk->i64TotalBytes - disk->i64FreeBytes) * 100) / disk->i64TotalBytes);
			m_disks.push_back(disk);
		} 
	} 
	if (h != INVALID_HANDLE_VALUE) 
		m_findVolumeMountPointClose(h);
}

bool			AgentDisk::GetDisksData() {
	TCHAR 			driveString[BUFFER_SIZE + 1];
	size_t			driveStringLen;
	LPTSTR			driveName;
	BOOL 			fResult;
	UINT  			errorMode;
	DWORD			pos, count, driveType;
	disk_t			*disk;
	
	memset(driveString, 0, (BUFFER_SIZE + 1) * sizeof(TCHAR));
	driveStringLen = GetLogicalDriveStrings(BUFFER_SIZE, (LPTSTR)driveString);
	if (driveStringLen == 0) {
		m_mgr.Log(LOGLEVEL_DEBUG, "GetLogicalDriveStrings failed");
		return false;
	}
	count = 0;
	errorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	for (pos = 0;pos < driveStringLen; ++count)  {
		driveName = &(driveString[pos]);
		driveType = GetDriveType(driveName);
		m_mgr.Log(LOGLEVEL_DEBUG, "drive string %s", driveName);
		if (driveType != DRIVE_REMOVABLE) { // no floppy 
			
			try {
				disk = new disk_t;
			} catch (std::bad_alloc ex) {
				continue ;
			}
			if (disk == NULL)
				continue ;
			ZeroMemory(disk, sizeof(*disk));
			disk->type = driveType;
			strncpy(disk->letter, driveName, 1);
			disk->letter[2] = '\0';
			fResult = GetDiskFreeSpaceEx(driveName, (PULARGE_INTEGER)&(disk->i64FreeBytesToCaller), 
				(PULARGE_INTEGER)&(disk->i64TotalBytes), (PULARGE_INTEGER)&(disk->i64FreeBytes));
			if (fResult != 0) {
				GetVolumeInformation(driveName, disk->volumeName, MAXNAMELEN, &(disk->volumeSerialNumber), 
				 &(disk->maximumComponentLength), &(disk->fileSystemFlags), disk->fileSystemName, MAXNAMELEN);
			}
			if (disk->i64TotalBytes > 0)
				disk->percent = (DWORD)(((disk->i64TotalBytes - disk->i64FreeBytes) * 100) / disk->i64TotalBytes);
			m_disks.push_back(disk);
			GetMountPointData(driveName);
		} 
		pos += strlen(driveName) + 1;
	}
	SetErrorMode(errorMode);
	return true;
}

DWORD	GetColorFromRule(const disk_t & disk, const disk_rule_t & rule) {
	DWORD	color = GREEN;

	// warning status
	if (rule.warnUseSize == true) {
		if (rule.warnSize > disk.i64FreeBytes)
			color = YELLOW;
	} else {
		if (rule.warnPercent < disk.percent)
			color = YELLOW;
	}
	// panic status
	if (rule.panicUseSize == true) {
		if (rule.panicSize > disk.i64FreeBytes)
			color = RED;
	} else {
		if (rule.panicPercent < disk.percent)
			color = RED;
	}
	return color;
}

DWORD	AgentDisk::ApplyRule(disk_t & disk) {
	map<std::string, disk_rule_t *>::iterator	itr;
	disk_rule_t		*rule;
	string			letter = disk.letter;
	DWORD			color;
	bool			useDefault = false;

	color = GREEN;
	itr = m_rules.find(letter);
	if (itr == m_rules.end()) {
		itr = m_rules.find(DEFAULT_RULE_NAME);
		useDefault = true;
	}
	rule = (*itr).second;
	rule->used = true;
	if (rule->ignore == true) {
		disk.ignore = true;
		return GREEN;
	}
	// check if cdrom or remote check is on. If specific rule, so check setting is understood as true
	if ((m_checkRemote == false && disk.type == DRIVE_REMOTE && (*itr).first == DEFAULT_RULE_NAME)
			|| (m_checkCdrom == false && disk.type == DRIVE_CDROM) && (*itr).first == DEFAULT_RULE_NAME) {
		disk.ignore = true;
		return GREEN;
	}
	color = GetColorFromRule(disk, *rule);
	if (disk.type == DRIVE_CDROM) {
		// if there is no specific rule, then we just check if a media is present
		// if there is a specific rule, then we check the free space
		// if no cdrom is inserted (totalbytes == 0) then we ignore the drive
		if (disk.i64TotalBytes > 0)
			if (useDefault)
				color = YELLOW;
			else
				color = GetColorFromRule(disk, *rule);
		else {
			disk.ignore = true;
			color = GREEN;
		}
	}
	return color;
}

void	AgentDisk::ApplyRules() {
	std::list<disk_t *>::iterator 				itr;
	map<std::string, disk_rule_t *>::iterator	itrRule;

	m_pageColor = GREEN;
	// clean used flag for rules
	for (itrRule = m_rules.begin(); itrRule != m_rules.end(); ++itrRule) {
		itrRule->second->used = false;
	}
	for (itr = m_disks.begin(); itr != m_disks.end(); ++itr) {	
		disk_t	& disk = *(*itr);
		disk.color = GREEN;
		if (disk.ignore || (disk.type == DRIVE_FIXED && disk.i64TotalBytes == 0))
			continue ;
		disk.color = ApplyRule(disk);
		if (m_pageColor < disk.color)
			m_pageColor = disk.color;
	}
}

void		AgentDisk::FreeDisksData() {
	std::list<disk_t *>::iterator 	itr;
	
	for (itr = m_disks.begin(); itr != m_disks.end(); ++itr) {
		delete (*itr);
	}
	m_disks.clear();
}

static void		FormatDiskData(__int64 & size, __int64 & avail, string & unit) {
	__int64		sizeSave;

	sizeSave = size;
	unit = "b";
	avail = 0;
	if ((sizeSave / 1024) > 0) {
		size = sizeSave / 1024;
		avail = (sizeSave - (1024 * size));
		unit = "kb";
	}
	if ((sizeSave / 1048576) > 0) {
		size = sizeSave / 1048576;
		avail = (sizeSave - (1048576 * size)) / 1024;
		unit = "mb";
	}
	if ((sizeSave / 1073741824) > 0) {
		size = sizeSave / 1073741824;
		avail = (sizeSave - (1073741824 * size)) / 1048576;
		unit = "gb";
	}
	if ((sizeSave / 1099511627776) > 0) {
		size = sizeSave / 1099511627776;
		avail = (sizeSave - (1099511627776 * size)) / 1073741824;
		unit = "tb";
	}	
	// used to get only 2 digits
	// not very efficient for the moment
	while (avail > 100) {
		avail /= 10;
	}
}

void		AgentDisk::GenerateSummary(const disk_t & disk, stringstream & summary) {
	__int64		size;
	__int64		avag;
	string		unit;
	
	size = disk.i64TotalBytes;
	FormatDiskData(size, avag, unit);
	summary << format("%f.%02u%s") % size % avag % unit;
	summary << "\\";
	size = disk.i64FreeBytes;
	FormatDiskData(size, avag, unit);
	summary << format("%f.%02u%s") % size % avag % unit;
}

void		AgentDisk::SendStatusReport() {
	stringstream 					reportData;	
	std::list<disk_t *>::iterator 	itr;
	map<std::string, disk_rule_t *>::iterator	itrRule;
	disk_t							*disk;

	if (m_mgr.IsCentralModeEnabled() == false) {
		ptime now = second_clock::local_time();
		reportData << to_simple_string(now) << " [" << m_mgr.GetSetting("hostname") << "] " << endl;
		reportData << "\n" << endl;
	}
	reportData << format("Filesystem     1K-blocks     Used       Avail    Capacity    Mounted      Summary(Total\\Avail)") << endl;
	for (itr = m_disks.begin(); itr != m_disks.end(); ++itr) {
		stringstream					summary;

		disk = (*itr);
		// we don't use ignore disk and fixed disk with totalbyte at 0
		if (disk->ignore || (disk->type == DRIVE_FIXED && disk->i64TotalBytes == 0))
			continue ;
		GenerateSummary(*disk, summary);
		reportData << format("%-13s %10.0f %10.0f %10.0f   %3d%%       /%s/%-8s %s") %
			disk->letter % (disk->i64TotalBytes / 1024) % ((disk->i64TotalBytes - disk->i64FreeBytes) / 1024) % 
			(disk->i64FreeBytes / 1024) % disk->percent % GetDiskTypeStr(disk->type) % 
			disk->letter % summary.str();
		if (m_mgr.IsCentralModeEnabled() == false)
			reportData << " &" << bbcolors[disk->color];
		reportData << endl;
	} 
	// check unused custum rules to generate an alert. Not used in centralized mode
	for (itrRule = m_rules.begin(); itrRule != m_rules.end(); ++itrRule) {
		float	size = 0.000;
		if (itrRule->second->used == false && itrRule->first != DEFAULT_RULE_NAME && itrRule->second->ignore == false) {
			reportData << format("%s             %10.0f %10.0f %10.0f   %3d%%       /%s/%s      %s") %
			itrRule->first % size % size % size % 0 % GetDiskTypeStr(DRIVE_UNKNOWN) % itrRule->first % "drive not found";
			reportData << " &" << bbcolors[RED];
			reportData << endl;
			m_pageColor = RED;
		}
	}
	if (m_mgr.IsCentralModeEnabled() == false)
		m_mgr.Status(m_testName.c_str(), bbcolors[m_pageColor], reportData.str().c_str());
	else
		m_mgr.ClientData(m_testName.c_str(), reportData.str().c_str());
}

void 		AgentDisk::Run() {
	GetDisksData();
	ApplyRules();
	if (m_alwaysgreen == true) 
		m_pageColor = GREEN;
	SendStatusReport();
	FreeDisksData();
}

AgentDisk::AgentDisk(IBBWinAgentManager & mgr) : m_mgr(mgr) {
	m_checkRemote = false;
	m_checkCdrom = false;
	m_alwaysgreen = false;
	m_testName = "disk";
	m_findFirstVolumeMountPoint = NULL;
	m_findNextVolumeMountPoint = NULL;
	m_findVolumeMountPointClose = NULL;
}

void		AgentDisk::BuildRule(disk_rule_t & rule, const string & warnlevel, const string & paniclevel) {
	map<std::string, disk_rule_t *>::iterator	itr;

	itr = m_rules.find(DEFAULT_RULE_NAME);
	if (itr == m_rules.end()) {
		rule.warnUseSize = false;
		rule.panicUseSize = false;
		rule.warnPercent = m_mgr.GetNbr(DEF_DISK_WARN);
		rule.panicPercent = m_mgr.GetNbr(DEF_DISK_PANIC);
	} else {
		disk_rule_t		* defRule = (*itr).second;
		rule.warnUseSize = defRule->warnUseSize;
		rule.panicUseSize = defRule->panicUseSize;
		rule.warnPercent = defRule->warnPercent;
		rule.panicPercent = defRule->panicPercent;
	}
	if (warnlevel.length() > 0) {
		if (GetSizeValue(warnlevel, rule.warnSize))
			rule.warnUseSize = true; 
		else {
			rule.warnPercent = m_mgr.GetNbr(warnlevel.c_str());	
			rule.warnUseSize = false; 
		}
	}
	if (paniclevel.length() > 0) {
		if (GetSizeValue(paniclevel, rule.panicSize))
			rule.panicUseSize = true;
		else {
			rule.panicPercent = m_mgr.GetNbr(paniclevel.c_str());
			rule.panicUseSize = false;
		}
	}
	if (rule.warnUseSize == false && rule.panicUseSize == false && rule.panicPercent < rule.warnPercent)
		m_mgr.Log(LOGLEVEL_WARN, "panic percentage must be higher than warning percentage");
	if (rule.warnUseSize == true && rule.panicUseSize == true && rule.panicSize > rule.warnSize)
		m_mgr.Log(LOGLEVEL_WARN, "warning free space must be higher than panic free space");
	if (rule.warnUseSize == true && rule.warnSize < 0)
		m_mgr.Log(LOGLEVEL_WARN, "invalid warning free space value");
	if (rule.panicUseSize == true && rule.panicSize < 0)
		m_mgr.Log(LOGLEVEL_WARN, "invalid panic free space value");
}

void		AgentDisk::AddRule(const string & label, const string & warnlevel, const string & paniclevel,
								const string & ignore) {
	disk_rule_t		*rule;
	
	try {
		rule = new disk_rule_t;
	} catch (std::bad_alloc ex) {
		return ;
	}
	if (rule == NULL)
		return ;
	ZeroMemory(rule, sizeof(*rule));
	rule->ignore = false;
	rule->used = false;
	if (ignore == "true")
		rule->ignore = true;
	BuildRule(*rule, warnlevel, paniclevel);
	if (m_rules.find(label) == m_rules.end()) {
		m_rules.insert(pair<std::string, disk_rule_t *>(label, rule));
	} else {
		string mess;
		mess = "duplicate rule " + label;
		m_mgr.Log(LOGLEVEL_WARN, mess.c_str());
		delete rule;
	}
}

bool		AgentDisk::Init() {
	m_mgr.Log(LOGLEVEL_DEBUG, "init disk agent %s", __FUNCTION__);
	HMODULE hm = GetModuleHandle("kernel32.dll");
	if (hm == NULL) {
		string err; 

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_ERROR, "GetModuleHandle(kernel32.dll) failed : %s", err.c_str());
	}
    m_findFirstVolumeMountPoint = (MYFINDFIRSTVOLUMEMOUNTPOINT)GetProcAddress(hm, "FindFirstVolumeMountPointA");
	if (m_findFirstVolumeMountPoint == NULL) {
		string err; 

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_DEBUG, "GetProcAddress(FindFirstVolumeMountPointA) failed : %s", err.c_str());
	}
	m_findNextVolumeMountPoint = (MYFINDNEXTVOLUMEMOUNTPOINT)GetProcAddress(hm, "FindNextVolumeMountPointA");
	if (m_findNextVolumeMountPoint == NULL) {
		string err; 

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_DEBUG, "GetProcAddress(FindNextVolumeMountPointA) failed : %s", err.c_str());
	}
	m_findVolumeMountPointClose = (MYFINDVOLUMEMOUNTPOINTCLOSE)GetProcAddress(hm, "FindVolumeMountPointClose");
	if (m_findVolumeMountPointClose == NULL) {
		string err; 

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_DEBUG, "GetProcAddress(FindVolumeMountPointClose) failed : %s", err.c_str());
	}
	if (m_mgr.IsCentralModeEnabled() == false) {
		PBBWINCONFIG		conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

		if (conf == NULL)
			return false;
		PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
		if (range == NULL)
			return false;
		for ( ; m_mgr.AtEndConfigurationRange(range); m_mgr.IterateConfigurationRange(range)) {
			string		name, value;

			name = m_mgr.GetConfigurationRangeValue(range, "name");
			value = m_mgr.GetConfigurationRangeValue(range, "value");
			if (name == "alwaysgreen") {
				if (value == "true")
					m_alwaysgreen = true;
			} else if (name == "remote") {
				if (value == "true")
					m_checkRemote = true;
			} else if (name == "testname") {
				if (value.length() > 0) 
					m_testName = value;
			} else if (name == "cdrom") {
				if (value == "true")
					m_checkCdrom = true;
			} else {
				AddRule(name, 
					m_mgr.GetConfigurationRangeValue(range, "warnlevel"), 
					m_mgr.GetConfigurationRangeValue(range, "paniclevel"), 
					m_mgr.GetConfigurationRangeValue(range, "ignore"));	
			}
		}
		
		m_mgr.FreeConfigurationRange(range);
		m_mgr.FreeConfiguration(conf);
	}
	// if no default, create the default rule
	if (m_rules.find(DEFAULT_RULE_NAME) == m_rules.end()) {
		AddRule(DEFAULT_RULE_NAME, DEF_DISK_WARN, DEF_DISK_PANIC, "false");
	}
	return true;
}

AgentDisk::~AgentDisk() {
	map<std::string, disk_rule_t *>::iterator		itr;
	
	for (itr = m_rules.begin(); itr != m_rules.end(); ++itr)
		delete itr->second;
	m_rules.clear();
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentDisk(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &diskAgentInfo;
}
