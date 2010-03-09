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
// $Id: BBWinService.cpp 96 2008-05-21 17:30:49Z sharpyy $

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <tchar.h>

#include "BBWinService.h"

#include "Utils.h"

using namespace utils;

// internal variables
SERVICE_STATUS          ssStatus;       // current status of the service
SERVICE_STATUS_HANDLE   sshStatusHandle;
DWORD                   dwErr = 0;
BOOL                    bDebug = FALSE;
TCHAR                   szErr[256];

// internal function prototypes
VOID WINAPI service_ctrl(DWORD dwCtrlCode);
VOID WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv);
VOID CmdInstallService();
VOID CmdRemoveService();
VOID CmdDebugService(DWORD argc, LPTSTR *argv);
BOOL WINAPI ControlHandler ( DWORD dwCtrlType );

//
//  FUNCTION: main
//
//  PURPOSE: entrypoint for service
//
//  PARAMETERS:
//    argc - number of command line arguments
//    argv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//    main() either performs the command line task, or
//    call StartServiceCtrlDispatcher to register the
//    main service thread.  When the this call returns,
//    the service has stopped, so exit.
//
void __cdecl main(int argc, LPTSTR *argv)
{
   SERVICE_TABLE_ENTRY dispatchTable[] =
   {
      { TEXT(SZSERVICENAME), (LPSERVICE_MAIN_FUNCTION)service_main},
      { NULL, NULL}
   };

   if ( (argc > 1) &&
        ((*argv[1] == '-') || (*argv[1] == '/')) )
   {
      if ( strcmp( _T("install"), argv[1]+1 ) == 0 )
      {
         CmdInstallService();
      }
      else if ( strcmp( _T("remove"), argv[1]+1 ) == 0 )
      {
         CmdRemoveService();
      }
      else if ( strcmp( _T("debug"), argv[1]+1 ) == 0 )
      {
         bDebug = TRUE;
         CmdDebugService(argc, argv);
      }
      else
      {
         goto dispatch;
      }
      exit(0);
   }

   // if it doesn't match any of the above parameters
   // the service control manager may be starting the service
   // so we must call StartServiceCtrlDispatcher
   dispatch:
   
   printf("\nBBWin is a free Big Brother and Hobbit client for Windows.\n");
   printf("This software is under GPL licence. Please read the licence file.\n\n");
   printf( "%s -install          to install the service\n", SZAPPNAME );
   printf( "%s -remove           to remove the service\n", SZAPPNAME );
   printf( "%s -debug <params>   to run as a console app for debugging\n", SZAPPNAME );
   
   printf( "\nStartServiceCtrlDispatcher being called.\n" );
   printf( "This may take several seconds.  Please wait.\n" );
   printf( "\n" );
      
   //StartServiceCtrlDispatcher(dispatchTable))
   //  AddToMessageLog(TEXT("StartServiceCtrlDispatcher failed."));
   StartServiceCtrlDispatcher(dispatchTable);
}



//
//  FUNCTION: service_main
//
//  PURPOSE: To perform actual initialization of the service
//
//  PARAMETERS:
//    dwArgc   - number of command line arguments
//    lpszArgv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//    This routine performs the service initialization and then calls
//    the user defined ServiceStart() routine to perform majority
//    of the work.
//
void WINAPI service_main(DWORD dwArgc, LPTSTR *lpszArgv)
{

   // register our service control handler:
   //
   sshStatusHandle = RegisterServiceCtrlHandler( TEXT(SZSERVICENAME), service_ctrl);

   if (!sshStatusHandle)
      goto cleanup;

   // SERVICE_STATUS members that don't change in example
   //
   ssStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
   ssStatus.dwServiceSpecificExitCode = 0;


   // report the status to the service control manager.
   //
   if (!ReportStatusToSCMgr(
                           SERVICE_START_PENDING, // service state
                           NO_ERROR,              // exit code
                           3000))                 // wait hint
      goto cleanup;


   ServiceStart( dwArgc, lpszArgv );

   cleanup:

   // try to report the stopped status to the service control manager.
   //
   if (sshStatusHandle)
      (VOID)ReportStatusToSCMgr(
                               SERVICE_STOPPED,
                               dwErr,
                               0);

   return;
}



//
//  FUNCTION: service_ctrl
//
//  PURPOSE: This function is called by the SCM whenever
//           ControlService() is called on this service.
//
//  PARAMETERS:
//    dwCtrlCode - type of control requested
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
VOID WINAPI service_ctrl(DWORD dwCtrlCode)
{
	DWORD dwState = SERVICE_RUNNING;
   // Handle the requested control code.
   //
   switch (dwCtrlCode)
   {
		case SERVICE_CONTROL_STOP:
			dwState = SERVICE_STOP_PENDING;
		break ;
		
		case SERVICE_CONTROL_SHUTDOWN:
			dwState = SERVICE_STOP_PENDING;
			break;
		
		case SERVICE_CONTROL_INTERROGATE:
			break;

      // invalid control code
      //
		default:
			break;
	}
	ReportStatusToSCMgr(dwState, NO_ERROR, 0);
	if ((dwCtrlCode == SERVICE_CONTROL_STOP) ||
       (dwCtrlCode == SERVICE_CONTROL_SHUTDOWN))
	{
		ServiceStop();
	}
}



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
//  COMMENTS:
//
BOOL ReportStatusToSCMgr(DWORD dwCurrentState,
                         DWORD dwWin32ExitCode,
                         DWORD dwWaitHint)
{
   static DWORD dwCheckPoint = 1;
   BOOL fResult = TRUE;


   if ( !bDebug ) // when debugging we don't report to the SCM
   {
      if (dwCurrentState == SERVICE_START_PENDING)
         ssStatus.dwControlsAccepted = 0;
      else
         ssStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

      ssStatus.dwCurrentState = dwCurrentState;
      ssStatus.dwWin32ExitCode = dwWin32ExitCode;
      ssStatus.dwWaitHint = dwWaitHint;

      if ( ( dwCurrentState == SERVICE_RUNNING ) ||
           ( dwCurrentState == SERVICE_STOPPED ) )
         ssStatus.dwCheckPoint = 0;
      else
         ssStatus.dwCheckPoint = dwCheckPoint++;

	  fResult = SetServiceStatus( sshStatusHandle, &ssStatus);
      // Report the status of the service to the service control manager.
      //
      //if (!())
      //{
       //  AddToMessageLog(TEXT("SetServiceStatus"));
      //}
   }
   return fResult;
}


//
//  FUNCTION: AddEventSource(LPTSTR pszLogName,  LPTSTR pszSrcName, LPTSTR pszMsgDLL, DWORD dwNum)
//
//  PURPOSE: Register to the event log
//
//  PARAMETERS:
//    pszLogName  Application log or a custom log
//    pszSrcName  event source name
//    pszMsgDLL path for message DLL
//    dwNum  DWORD  
//  RETURN VALUE:
//    bool success
//
//  COMMENTS:
//
bool 		AddEventSource(LPTSTR pszLogName, LPTSTR pszSrcName, LPTSTR pszMsgDLL, DWORD dwNum)
{
   HKEY 	hk; 
   DWORD 	dwData, dwDisp; 
   TCHAR 	szBuf[MAX_PATH + 1]; 

   // Create the event source as a subkey of the log. 
   wsprintf(szBuf, 
      "SYSTEM\\CurrentControlSet\\Services\\EventLog\\%s\\%s",
      pszLogName, pszSrcName); 
 
   if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, szBuf, 
          0, NULL, REG_OPTION_NON_VOLATILE,
          KEY_WRITE, NULL, &hk, &dwDisp)) 
   {
        return false;
   }
 
   // Set the name of the message file. 
 
   if (RegSetValueEx(hk,              // subkey handle 
           "EventMessageFile",        // value name 
           0,                         // must be zero 
           REG_EXPAND_SZ,             // value type 
           (LPBYTE) pszMsgDLL,        // pointer to value data 
           (DWORD) lstrlen(pszMsgDLL)+1)) // length of value data 
   {
      RegCloseKey(hk); 
      return false;
   }
   // Set the supported event types. 
   dwData = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | 
        EVENTLOG_INFORMATION_TYPE; 
   if (RegSetValueEx(hk,      // subkey handle 
           "TypesSupported",  // value name 
           0,                 // must be zero 
           REG_DWORD,         // value type 
           (LPBYTE) &dwData,  // pointer to value data 
           sizeof(DWORD)))    // length of value data 
   {
      RegCloseKey(hk); 
      return false;
   }
   
   if (RegSetValueEx(hk,              // subkey handle 
           "CategoryMessageFile",     // value name 
           0,                         // must be zero 
           REG_EXPAND_SZ,             // value type 
           (LPBYTE) pszMsgDLL,        // pointer to value data 
           (DWORD) lstrlen(pszMsgDLL)+1)) // length of value data 
   {
      RegCloseKey(hk); 
      return FALSE;
   }

   if (RegSetValueEx(hk,      // subkey handle 
           "CategoryCount",   // value name 
           0,                 // must be zero 
           REG_DWORD,         // value type 
           (LPBYTE) &dwNum,   // pointer to value data 
           sizeof(DWORD)))    // length of value data 
   {
      RegCloseKey(hk); 
      return FALSE;
   }
   
   RegCloseKey(hk); 
   return true;
}

//
//  FUNCTION: DeleteEventSource(LPTSTR pszLogName,  LPTSTR pszSrcName)
//
//  PURPOSE: Unregister the event log
//
//  PARAMETERS:
//    pszLogName  Application log or a custom log
//    pszSrcName  event source name
//  
//  RETURN VALUE:
//    bool success
//
//  COMMENTS:
//
bool 		DeleteEventSource(LPTSTR pszLogName, LPTSTR pszSrcName)
{
	HKEY 	hk; 
	TCHAR 	szBuf[MAX_PATH + 1]; 

	wsprintf(szBuf, 
      "SYSTEM\\CurrentControlSet\\Services\\EventLog\\%s",
      pszLogName, pszSrcName); 
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, szBuf, 0, KEY_WRITE, &hk)) 
		return false;
	RegDeleteKey(hk, pszSrcName);
	RegCloseKey(hk); 
	return true;
}


///////////////////////////////////////////////////////////////////
//
//  The following code handles service installation and removal
//


//
//  FUNCTION: CmdInstallService()
//
//  PURPOSE: Installs the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdInstallService()
{
   SC_HANDLE   schService;
   SC_HANDLE   schSCManager;
   SERVICE_DESCRIPTION 		schSCDescription;
   TCHAR szPath[512];

	schSCDescription.lpDescription = SZDESCRIPTION;
   if ( GetModuleFileName( NULL, szPath, 512 ) == 0 )
   {
      _tprintf(TEXT("Unable to install %s - %s\n"), TEXT(SZSERVICEDISPLAYNAME), GetLastErrorText(szErr, 256));
      return;
   }

   schSCManager = OpenSCManager(
                               NULL,                   // machine (NULL == local)
                               NULL,                   // database (NULL == default)
                               SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE  // access required
                               );
   if ( schSCManager )
   {
      schService = CreateService(
                                schSCManager,               // SCManager database
                                TEXT(SZSERVICENAME),        // name of service
                                TEXT(SZSERVICEDISPLAYNAME), // name to display
                                SERVICE_QUERY_STATUS,         // desired access
                                SERVICE_WIN32_OWN_PROCESS,  // service type
                                SERVICE_DEMAND_START,       // start type
                                SERVICE_ERROR_NORMAL,       // error control type
                                szPath,                     // service's binary
                                NULL,                       // no load ordering group
                                NULL,                       // no tag identifier
                                TEXT(SZDEPENDENCIES),       // dependencies
                                NULL,                       // LocalSystem account
                                NULL);                      // no password
								

      if ( schService )
      {
         _tprintf(TEXT("%s installed.\n"), TEXT(SZSERVICEDISPLAYNAME) );
		 
		
         CloseServiceHandle(schService);

		// ChangeServiceConfig2 not compatible with NT 4
		//schService = OpenService(schSCManager, TEXT(SZSERVICENAME), SERVICE_CHANGE_CONFIG );
		//if ( schService )
		//{
			
			//if (ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &schSCDescription) == 0) {	
			//	_tprintf(TEXT("ChangeServiceConfig2 failed - %i %s\n"), GetLastError(), GetLastErrorText(szErr, 256));
			//}
			//CloseServiceHandle(schService);
		//} else  {
			//_tprintf(TEXT("ChangeServiceConfig2 failed - %i %s\n"), GetLastError(), GetLastErrorText(szErr, 256));
		//}
		
		AddEventSource(SZEVENTLOG, SZEVENTLOGNAME, szPath, 2);
      }
      else
      {
         _tprintf(TEXT("CreateService failed - %s\n"), GetLastErrorText(szErr, 256));
      }

      CloseServiceHandle(schSCManager);
   }
   else
      _tprintf(TEXT("OpenSCManager failed - %s\n"), GetLastErrorText(szErr,256));
}



//
//  FUNCTION: CmdRemoveService()
//
//  PURPOSE: Stops and removes the service
//
//  PARAMETERS:
//    none
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdRemoveService()
{
   SC_HANDLE   schService;
   SC_HANDLE   schSCManager;

   schSCManager = OpenSCManager(
                               NULL,                   // machine (NULL == local)
                               NULL,                   // database (NULL == default)
                               SC_MANAGER_CONNECT   // access required
                               );
   if ( schSCManager )
   {
      schService = OpenService(schSCManager, TEXT(SZSERVICENAME), DELETE | SERVICE_STOP | SERVICE_QUERY_STATUS);

      if (schService)
      {
         // try to stop the service
         if ( ControlService( schService, SERVICE_CONTROL_STOP, &ssStatus ) )
         {
            _tprintf(TEXT("Stopping %s."), TEXT(SZSERVICEDISPLAYNAME));
            Sleep( 1000 );

            while ( QueryServiceStatus( schService, &ssStatus ) )
            {
               if ( ssStatus.dwCurrentState == SERVICE_STOP_PENDING )
               {
                  _tprintf(TEXT("."));
                  Sleep( 1000 );
               }
               else
                  break;
            }

            if ( ssStatus.dwCurrentState == SERVICE_STOPPED )
               _tprintf(TEXT("\n%s stopped.\n"), TEXT(SZSERVICEDISPLAYNAME) );
            else
               _tprintf(TEXT("\n%s failed to stop.\n"), TEXT(SZSERVICEDISPLAYNAME) );

         }

        // now remove the service
        if ( DeleteService(schService) )
		{
            _tprintf(TEXT("%s removed.\n"), TEXT(SZSERVICEDISPLAYNAME) );
			DeleteEventSource(SZEVENTLOG, SZEVENTLOGNAME);
		}
         else
            _tprintf(TEXT("DeleteService failed - %s\n"), GetLastErrorText(szErr,256));


         CloseServiceHandle(schService);
      }
      else
         _tprintf(TEXT("OpenService failed - %s\n"), GetLastErrorText(szErr,256));

      CloseServiceHandle(schSCManager);
   }
   else
      _tprintf(TEXT("OpenSCManager failed - %s\n"), GetLastErrorText(szErr,256));
}




///////////////////////////////////////////////////////////////////
//
//  The following code is for running the service as a console app
//


//
//  FUNCTION: CmdDebugService(int argc, char ** argv)
//
//  PURPOSE: Runs the service as a console application
//
//  PARAMETERS:
//    argc - number of command line arguments
//    argv - array of command line arguments
//
//  RETURN VALUE:
//    none
//
//  COMMENTS:
//
void CmdDebugService(DWORD argc, LPTSTR * argv)
{
	printf(TEXT("Debugging %s.\n"), TEXT(SZSERVICEDISPLAYNAME));
	SetConsoleCtrlHandler( ControlHandler, TRUE );
	ServiceStart(argc, argv );
}


//
//  FUNCTION: ControlHandler ( DWORD dwCtrlType )
//
//  PURPOSE: Handled console control events
//
//  PARAMETERS:
//    dwCtrlType - type of control event
//
//  RETURN VALUE:
//    True - handled
//    False - unhandled
//
//  COMMENTS:
//
BOOL WINAPI ControlHandler ( DWORD dwCtrlType )
{
   switch ( dwCtrlType )
   {
   case CTRL_BREAK_EVENT:  // use Ctrl+C or Ctrl+Break to simulate
   case CTRL_C_EVENT:      // SERVICE_CONTROL_STOP in debug mode
      _tprintf(TEXT("Stopping %s.\n"), TEXT(SZSERVICEDISPLAYNAME));
      ServiceStop();
      return TRUE;
      break;
   }
   return FALSE;
}
