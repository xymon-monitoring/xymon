#ifndef __BBWINMESSAGES_H__
#define __BBWINMESSAGES_H__

////////////////////////////////////////
// Eventlog categories
//
// These always have to be the first entries in a message file
//
//
//  Values are 32 bit values layed out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//


//
// Define the severity codes
//


//
// MessageId: BBWIN_SERVICE
//
// MessageText:
//
//  Service
//
#define BBWIN_SERVICE                    0x00000001L

//
// MessageId: BBWIN_AGENT
//
// MessageText:
//
//  Agent
//
#define BBWIN_AGENT                      0x00000002L

////////////////////////////////////////
// Events
//
//
// MessageId: EVENT_NO_REGISTRY
//
// MessageText:
//
//  The service didn't successfully load the registry key %1. Please check the service installation
//
#define EVENT_NO_REGISTRY                0x00000003L

//
// MessageId: EVENT_NO_CONFIGURATION
//
// MessageText:
//
//  The service didn't successfully load the configuration file %1. The reason was : %2
//
#define EVENT_NO_CONFIGURATION           0x00000004L

//
// MessageId: EVENT_INVALID_CONFIGURATION
//
// MessageText:
//
//  The configuration file %1 in namespace %2 has an unknown %3 : %4
//
#define EVENT_INVALID_CONFIGURATION      0x00000005L

//
// MessageId: EVENT_LOAD_AGENT_ERROR
//
// MessageText:
//
//  The agent %1 has been not loaded. The reason was : %2
//
#define EVENT_LOAD_AGENT_ERROR           0x00000006L

//
// MessageId: EVENT_NO_BB_DISPLAY
//
// MessageText:
//
//  No bbdisplay value has been specified in the configuration file %1. At least one bbdisplay setting is needed to run bbwin
//
#define EVENT_NO_BB_DISPLAY              0x00000007L

//
// MessageId: EVENT_NO_HOSTNAME
//
// MessageText:
//
//  An empty hostname value has been specified. This setting is necessary.
//
#define EVENT_NO_HOSTNAME                0x00000008L

//
// MessageId: EVENT_BAD_PATH
//
// MessageText:
//
//  Can't reach the directory %1.
//
#define EVENT_BAD_PATH                   0x00000009L

//
// MessageId: EVENT_LOAD_AGENT_SUCCESS
//
// MessageText:
//
//  The agent %1 has been successfully loaded. %2
//
#define EVENT_LOAD_AGENT_SUCCESS         0x0000000AL

//
// MessageId: EVENT_ALREADY_LOADED_AGENT
//
// MessageText:
//
//  The agent with name %1 has been already loaded. Please check the agent name in the configuration file.
//
#define EVENT_ALREADY_LOADED_AGENT       0x0000000BL

//
// MessageId: EVENT_UNCOMPATIBLE_AGENT
//
// MessageText:
//
//  The agent %1 is not compatible with this bbwin version.
//
#define EVENT_UNCOMPATIBLE_AGENT         0x0000000CL

//
// MessageId: EVENT_CANT_INSTANTIATE_AGENT
//
// MessageText:
//
//  The agent %1 has been not instantiated. The reason was : %2
//
#define EVENT_CANT_INSTANTIATE_AGENT     0x0000000DL

//
// MessageId: EVENT_SERVICE_RELOAD
//
// MessageText:
//
//  The configuration file has changed. BBWin is reloading the configuration.
//
#define EVENT_SERVICE_RELOAD             0x0000000EL

//
// MessageId: EVENT_SERVICE_STARTED
//
// MessageText:
//
//  The service %1 has been successfully started. The hostname setting is set to "%2"
//
#define EVENT_SERVICE_STARTED            0x0000000FL

//
// MessageId: EVENT_SERVICE_STOPPED
//
// MessageText:
//
//  The service %1 has been successfully stopped.
//
#define EVENT_SERVICE_STOPPED            0x00000010L

//
// MessageId: EVENT_MESSAGE_AGENT
//
// MessageText:
//
//  The agent %1 generated this event message : %2
//
#define EVENT_MESSAGE_AGENT              0x00000011L

//
// MessageId: EVENT_INVALID_TIMER
//
// MessageText:
//
//  The personnalized timer for agent %1 is incorrect. Minimum value is 5 seconds and maximum value is 31 days. It will use default value %2 seconds
//
#define EVENT_INVALID_TIMER              0x00000012L

//
// MessageId: EVENT_HOBBIT_MODE_NOT_SUPPORTED
//
// MessageText:
//
//  The agent does not support hobbit centralized mode. It will be launched as a classical BBWin agent.
//
#define EVENT_HOBBIT_MODE_NOT_SUPPORTED  0x00000013L

//
// MessageId: EVENT_HOBBIT_FAILED_CLIENTDATA
//
// MessageText:
//
//  BBWin failed to send the client data successfuly to the hobbit server. The error was : %1 : %2.
//
#define EVENT_HOBBIT_FAILED_CLIENTDATA   0x00000014L

////////////////////////////////////////
// Additional messages
//
//MessageId       = 1000
//SymbolicName    = IDS_HELLO
//Language        = English
//Hello World!
//.

#endif  //!__BBWINMESSAGES_H__

