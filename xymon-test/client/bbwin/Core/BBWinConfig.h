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

#ifndef __BBWINCONFIG_H__
#define	__BBWINCONFIG_H__

#include <map>
#include <string>
#include <iostream>

#include "Singleton.h"
#include "IBBWinException.h"

#include "tinyxml.h"

using namespace DesignPattern;

#define		BBWIN_CONFIG_ROOT 		"//configuration/"

typedef std::map < std::string, std::string > 		bbwinconfig_attr_t;
typedef std::multimap < std::string, bbwinconfig_attr_t > bbwinconfig_t;
typedef bbwinconfig_t::iterator config_iter_t;

class BBWinConfig : public Singleton< BBWinConfig > {
	private :
		std::string				m_path;
		TiXmlDocument 			*m_doc;
		
	public :
		BBWinConfig();
		~BBWinConfig();
		
		void			LoadConfiguration(const std::string & fileName, const std::string & configNameSpace, bbwinconfig_t & config);
};

/** class BBWinConfigException 
*/
class BBWinConfigException : IBBWinException {
public:
	BBWinConfigException(const char* m);
	std::string getMessage() const;
};	



#endif // __BBWINCONFIG__

