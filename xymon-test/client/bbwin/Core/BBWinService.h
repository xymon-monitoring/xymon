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
// $Id: BBWinService.h 96 2008-05-21 17:30:49Z sharpyy $


#ifndef _BBWINSERVICE_H
#define _BBWINSERVICE_H

#ifdef __cplusplus
extern "C" {
#endif


//////////////////////////////////////////////////////////////////////////////
////
// name of the executable
#define SZAPPNAME            "BBWin"
// internal name of the service
#define SZSERVICENAME        "BBWin"
// displayed name of the service
#define SZSERVICEDISPLAYNAME "Big Brother Hobbit Client"
// list of service dependencies - "dep1\0dep2\0\0"
#define SZDEPENDENCIES       ""
// description
#define SZDESCRIPTION			"Big Brother - Hobbit Windows Client"
//
// Event Log category
#define SZEVENTLOGNAME       "BigBrotherHobbitClient"
#define SZEVENTLOG			"Application"
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
//// todo: ServiceStart()must be defined by in your code.
////       The service should use ReportStatusToSCMgr to indicate
////       progress.  This routine must also be used by StartService()
////       to report to the SCM when the service is running.
////
////       If a ServiceStop procedure is going to take longer than
////       3 seconds to execute, it should spawn a thread to
////       execute the stop code, and return.  Otherwise, the
////       ServiceControlManager will believe that the service has
////       stopped responding
////
   VOID ServiceStart(DWORD dwArgc, LPTSTR *lpszArgv);
   VOID ServiceStop();
//////////////////////////////////////////////////////////////////////////////


//
//  FUNCTION: ReportStatusToSCMgr()
//
//  PURPOSE: Sets the current status of the service and
//           reports it to the Service Control Manager
//
//  PARAMETERS:
//    dwCurrentState - the state of the service
//    dwWin32ExitCode - error code to report
//    dwWaitHint - worst case estimate to next checkpoint
//
//  RETURN VALUE:
//    TRUE  - success
//    FALSE - failure
//
   BOOL ReportStatusToSCMgr(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);


//////////////////////////////////////////////////////////////////////////////



#ifdef __cplusplus
}
#endif

#endif
