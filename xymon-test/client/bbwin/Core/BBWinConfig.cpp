//this file is part of BBWin
//Copyright (C)2006-2008 Etienne GRIGNON  ( etienne.grignon@gmail.com 
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
// $Id: BBWinConfig.cpp 96 2008-05-21 17:30:49Z sharpyy $

#include <windows.h>
 
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <fstream>

#include "BBWinConfig.h"


using namespace std;

CRITICAL_SECTION  		m_configCriticalSection;

//
// XML Configuration Loading
//
//

BBWinConfig::BBWinConfig() {
	InitializeCriticalSection(&m_configCriticalSection);
}

BBWinConfig::~BBWinConfig() {
	DeleteCriticalSection(&m_configCriticalSection);
}

void 			BBWinConfig::LoadConfiguration(const string & fileName, const string & configNameSpace, bbwinconfig_t  & config) {
	string				configName(BBWIN_CONFIG_ROOT);

	EnterCriticalSection(&m_configCriticalSection); 
	m_path = fileName;
	m_doc = new TiXmlDocument(m_path.c_str());
	if (m_doc == NULL) 
		throw BBWinConfigException("can't allocare tinyxml instance");
	bool loadOkay = m_doc->LoadFile();
	if ( !loadOkay ) {
		LeaveCriticalSection(&m_configCriticalSection);
		throw BBWinConfigException(m_doc->ErrorDesc());
	}
	TiXmlElement *root = m_doc->FirstChildElement( "configuration" );
	if ( root ) {
		TiXmlNode * nameSpaceNode = root->FirstChild( configNameSpace.c_str() );
		if ( nameSpaceNode ) {
			TiXmlElement		* elem;
			for (elem = nameSpaceNode->FirstChildElement(); elem ; elem = elem->NextSiblingElement()) {
				bbwinconfig_attr_t		config_attr;
				if (elem->Type() == TiXmlNode::ELEMENT) {
					TiXmlAttribute 			* attr;
					
					for (attr = elem->FirstAttribute(); attr; attr = attr->Next()) {
						config_attr.insert(pair< string, string >(attr->Name(), attr->Value()));
					}
				}
				config.insert(pair < string, bbwinconfig_attr_t > (elem->Value(), config_attr));
			}
		}
	}
	delete m_doc;
	m_doc = NULL;
	LeaveCriticalSection(&m_configCriticalSection);
}

// BBWinConfigException
BBWinConfigException::BBWinConfigException(const char* m) {
	msg = m;
}

string BBWinConfigException::getMessage() const {
	return msg;
}
