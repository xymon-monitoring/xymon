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
// $Id: bbwinupdate.cpp 94 2008-05-21 17:18:28Z sharpyy $

#define BBWIN_AGENT_EXPORTS

#include <windows.h>
#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include "IBBWinAgent.h"
#include <list>
#include "tinyxml.h"
#include "digest.h"
#include "utils.h"
#include "BBWinUpdate.h"

using namespace std;

static const BBWinAgentInfo_t 		bbwinupdateAgentInfo =
{
	BBWIN_AGENT_VERSION,				        // bbwinVersion;
	"bbwinupdate",    					        // agentName;
	"bbwinupdate agent : used to update",        // agentDescription;
	BBWIN_AGENT_CENTRALIZED_COMPATIBLE 
};                

// return true if the 2 files are identical
static bool			Compare2Files(const char * file1, const char * file2) { 
	digestctx_t *dig1, *dig2;
	HANDLE			hFile1, hFile2;
	string			res1, res2;
	char			buf[1024];
	DWORD			read;
	LPTSTR			res;

	dig1 = digest_init("md5");
	dig2 = digest_init("md5");
	if (dig1 == NULL || dig2 == NULL)
		return false;
	hFile1 = CreateFile(file1,     // file to open
		GENERIC_READ,         // open for reading
		NULL, // do not share
		NULL,                   // default security
		OPEN_EXISTING, // default flags
		0,   // asynchronous I/O
		0);                  // no attr. template
	if (hFile1 == INVALID_HANDLE_VALUE) 
		return false;
	while (ReadFile(hFile1, buf, 1024, &read, NULL)) {
		if (read == 0)
			break ;
		digest_data(dig1, (unsigned char *)buf, read);
	}
	CloseHandle(hFile1);
	res = digest_done(dig1);
	res1 = res;
	if (res) free(res);
	hFile2 = CreateFile(file2,   // file to open
		GENERIC_READ,            // open for reading
		NULL,                    // do not share
		NULL,                    // default security
		OPEN_EXISTING,           // default flags
		0,                       // asynchronous I/O
		0);                      // no attr. template
	if (hFile2 == INVALID_HANDLE_VALUE) 
		return false;
	while (ReadFile(hFile2, buf, 1024, &read, NULL)) {
		if (read == 0)
			break ;
		digest_data(dig2, (unsigned char *)buf, read);
	}
	CloseHandle(hFile2);
	res = digest_done(dig2);
	res2 = res;
	if (res) free(res);
	if (res1 == res2)
		return true;
	return false;
}



// check if the xml is well formed
// check if the checksum is different
// return true if validated
bool					AgentBBWinUpdate::ValidateUpdate() {
	bool				valid = true;
	TiXmlDocument		*update;

	update = new TiXmlDocument(m_bbwinCfgTmpPath.c_str());
	if (update == NULL)
		return valid;
	bool loadUpdateOkay = update->LoadFile();
	if ( !loadUpdateOkay ) {
		string err = (string)" failed to validate the update file " 
			+ m_bbwinCfgTmpPath + (string) " : " + (string)update->ErrorDesc();
		m_mgr.ReportEventError(err.c_str());
		valid = false;
	}
	delete update;
	if (valid == false)
		return false;
	if (Compare2Files(m_bbwinCfgPath.c_str(), m_bbwinCfgTmpPath.c_str())) {
		valid = false;
	}
	return valid;
}

// get the configuration file using hobbit protocol config
void		AgentBBWinUpdate::RunUpdate(std::string & configFile) {
	TiXmlDocument		* update, * toUpdate;

	DeleteFile(m_bbwinupdateTmpFilePath.c_str());
	m_mgr.Config(configFile.c_str(), m_bbwinupdateTmpFilePath.c_str());
	update = new TiXmlDocument(m_bbwinupdateTmpFilePath.c_str());
	bool loadUpdateOkay = update->LoadFile();
	if ( !loadUpdateOkay ) {
		string err = (string)" failed to get the update " + configFile + (string)" or the update file is not correct";
		m_mgr.ReportEventError(err.c_str());
	}
	toUpdate = new TiXmlDocument(m_bbwinCfgTmpPath.c_str());
	bool loadToUpdateOkay = toUpdate->LoadFile();
	if ( !loadToUpdateOkay ) {
		delete update;
		string err = (string)" failed to open " + m_bbwinCfgTmpPath;
		m_mgr.ReportEventError(err.c_str());
	}
	TiXmlElement *root = update->FirstChildElement( "configuration" );
	TiXmlElement *toUpdateRoot = toUpdate->FirstChildElement( "configuration" );
	if ( root && toUpdateRoot) {
		for (TiXmlNode * nameSpaceNode = root->FirstChild(); nameSpaceNode != NULL; nameSpaceNode = root->IterateChildren(nameSpaceNode)) {
			// we never update bbwin namespace (too dangerous)
			if (strcmp(nameSpaceNode->Value(), "bbwin") != 0) {
				TiXmlNode * destNameSpaceNode = toUpdateRoot->FirstChild(nameSpaceNode->Value());
				if ( destNameSpaceNode ) {
					toUpdateRoot->ReplaceChild(destNameSpaceNode, *nameSpaceNode);
				} else {
					toUpdateRoot->InsertEndChild(*nameSpaceNode);
				}
			} else {
				string err = (string)" bbwin namespace update is not permitted. Please check the " + (string)configFile + (string)" on your hobbit server.";
				m_mgr.ReportEventError(err.c_str());
			}
		}
	}
	if (toUpdate->SaveFile() != true) {
		string err = (string)" failed to save " + m_bbwinCfgTmpPath;
		m_mgr.ReportEventError(err.c_str());
	}
	delete update;
	delete toUpdate;
}

void		AgentBBWinUpdate::RunAgentUpdate() {
	string clientVersionPath = m_mgr.GetSetting("etcpath") + "\\clientversion.cfg";
	string tmpPath = m_mgr.GetSetting("tmppath");
	string binpath = m_mgr.GetSetting("binpath");

	//get current version
	ifstream	versionFile(clientVersionPath.c_str());
	if (!versionFile) {
		string	err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_DEBUG, "can't open %s : %s", clientVersionPath.c_str(), err.c_str());
		return ;
	}
	string	localVersion;
	std::getline(versionFile, localVersion);
	versionFile.close();
	m_mgr.ClientData("clientversion", localVersion.c_str());

	// get the version from clientlocal.cfg
	string clientLocalCfgPath = tmpPath + (string)"\\clientlocal.cfg";
	ifstream		conf(clientLocalCfgPath.c_str());
	if (!conf) {
		string	err;

		utils::GetLastErrorString(err);
		m_mgr.Log(LOGLEVEL_INFO, "can't open %s : %s", clientLocalCfgPath.c_str(), err.c_str());
		return ;
	}
	string		buf;
	string		serverVersion;
	while (!conf.eof()) {
		string		value;

		utils::GetConfigLine(conf, buf);
		if (utils::parseStrGetNext(buf, "clientversion:", value)) {
			serverVersion = value;
		}
	}
	if (serverVersion == "") {
		m_mgr.Log(LOGLEVEL_DEBUG, "no clientversion specified client-local.cfg");
		return ;
	}
	if (serverVersion == localVersion) {
		m_mgr.Log(LOGLEVEL_DEBUG, "we have the latest version : %s", localVersion.c_str());
		return ;
	}
	//string	sysTempPath;
	//utils::GetEnvironmentVariableA("TEMP", sysTempPath);
	//serverVersion += ".zip";
	//string downloadPath  = sysTempPath + "\\" + serverVersion;
	//m_mgr.Log(LOGLEVEL_DEBUG, "download %s to %s", serverVersion.c_str(), tmpPath.c_str());
	//m_mgr.Download(serverVersion.c_str(), tmpPath.c_str());
}

void 		AgentBBWinUpdate::Run() {
	std::list<string>::iterator		itr;
	
	if (CopyFile(m_bbwinCfgPath.c_str(), m_bbwinCfgTmpPath.c_str(), false) == 0) {
		string err = (string)" failed to copy " + m_bbwinCfgPath + (string)" to " + m_bbwinCfgTmpPath;
		m_mgr.ReportEventError(err.c_str());
	}
	for (itr = m_configFiles.begin(); itr != m_configFiles.end(); ++itr) {
		RunUpdate((*itr));
	}
	if (ValidateUpdate()) {
		if (CopyFile(m_bbwinCfgTmpPath.c_str(), m_bbwinCfgPath.c_str(), false) == 0) {
			string err = (string)" failed to update " + m_bbwinCfgPath;
			m_mgr.ReportEventError(err.c_str());
		} else {
			string err = (string)" successfully updated the bbwin configuration";
			m_mgr.ReportEventInfo(err.c_str());
		}
	}
	// centralized mode
	if (m_mgr.IsCentralModeEnabled())
		RunAgentUpdate();
}

AgentBBWinUpdate::AgentBBWinUpdate(IBBWinAgentManager & mgr) : 
			m_mgr(mgr)
{
	m_bbwinCfgPath = m_mgr.GetSetting("etcpath") + (string)"\\bbwin.cfg";
	m_bbwinCfgTmpPath = m_mgr.GetSetting("tmppath") + (string)"\\bbwin.cfg.work";
	m_bbwinupdateTmpFilePath = m_mgr.GetSetting("tmppath") + (string)"\\bbwin.cfg.update";
}

bool		AgentBBWinUpdate::Init() {
	PBBWINCONFIG	conf = m_mgr.LoadConfiguration(m_mgr.GetAgentName());

	if (conf == NULL)
		return false;
	PBBWINCONFIGRANGE range = m_mgr.GetConfigurationRange(conf, "setting");
	if (range == NULL)
		return false;
	do {
		string name, value;
		
		name = m_mgr.GetConfigurationRangeValue(range, "name");
		value = m_mgr.GetConfigurationRangeValue(range, "value");
		if (name == "filename" && value.length() > 0) {
			TCHAR		buf[FILENAME_MAX];
			string		filename;

			ExpandEnvironmentStrings(value.c_str(), buf, FILENAME_MAX);
			filename = buf;
			m_configFiles.push_back(filename);
		}
	}
	while (m_mgr.IterateConfigurationRange(range));
	m_mgr.FreeConfigurationRange(range);
	m_mgr.FreeConfiguration(conf);
	return true;
}

BBWIN_AGENTDECL IBBWinAgent * CreateBBWinAgent(IBBWinAgentManager & mgr)
{
	return new AgentBBWinUpdate(mgr);
}

BBWIN_AGENTDECL void		 DestroyBBWinAgent(IBBWinAgent * agent)
{
	delete agent;
}

BBWIN_AGENTDECL const BBWinAgentInfo_t * GetBBWinAgentInfo() {
	return &bbwinupdateAgentInfo;
}


