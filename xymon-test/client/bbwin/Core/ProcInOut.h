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
// $Id: ProcInOut.h 70 2008-01-31 14:05:39Z sharpyy $

#ifndef		__PROCINOUT_H__
#define		__PROCINOUT_H__

#include <string>

namespace utils {

#define BUFSIZE 4096 

	class	ProcInOut {

	protected :
		HANDLE				hChildStdoutRd;
		HANDLE  			hChildStdoutWr;
		HANDLE  			hChildStdoutRdDup;
		HANDLE  			hSaveStdout;
		SECURITY_ATTRIBUTES saAttr;
		PROCESS_INFORMATION piProcInfo; 

	protected :
		bool				CreateChildProcess(const std::string & cmd);
		void				ReadFromPipe(std::string & out);

	public :
		ProcInOut() {};
		bool	Exec(const std::string & cmd, std::string & out, DWORD timeout);

	};


} /// namespace utils


#endif // !__PROCINOUT_H__


