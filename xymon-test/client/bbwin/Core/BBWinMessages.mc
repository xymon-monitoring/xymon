;#ifndef __BBWINMESSAGES_H__
;#define __BBWINMESSAGES_H__
;

LanguageNames =
    (
        English = 0x0409:Messages_ENU
    )


;////////////////////////////////////////
;// Eventlog categories
;//
;// These always have to be the first entries in a message file
;//

MessageId       = 1
SymbolicName    = BBWIN_SERVICE
Severity		= Success
Language        = English
Service
.

MessageId       = +1
SymbolicName    = BBWIN_AGENT
Severity		= Success
Language        = English
Agent
.

;////////////////////////////////////////
;// Events
;//

MessageId       = +1
SymbolicName    = EVENT_NO_REGISTRY
Language        = English
The service didn't successfully load the registry key %1. Please check the service installation
.

MessageId       = +1
SymbolicName    = EVENT_NO_CONFIGURATION
Language        = English
The service didn't successfully load the configuration file %1. The reason was : %2
.

MessageId       = +1
SymbolicName    = EVENT_INVALID_CONFIGURATION
Language        = English
The configuration file %1 in namespace %2 has an unknown %3 : %4
.

MessageId       = +1
SymbolicName    = EVENT_LOAD_AGENT_ERROR
Language        = English
The agent %1 has been not loaded. The reason was : %2
.

MessageId       = +1
SymbolicName    = EVENT_NO_BB_DISPLAY
Language        = English
No bbdisplay value has been specified in the configuration file %1. At least one bbdisplay setting is needed to run bbwin
.

MessageId       = +1
SymbolicName    = EVENT_NO_HOSTNAME
Language        = English
An empty hostname value has been specified. This setting is necessary.
.

MessageId       = +1
SymbolicName    = EVENT_BAD_PATH
Language        = English
Can't reach the directory %1.
.

MessageId       = +1
SymbolicName    = EVENT_LOAD_AGENT_SUCCESS
Language        = English
The agent %1 has been successfully loaded. %2
.

MessageId       = +1
SymbolicName    = EVENT_ALREADY_LOADED_AGENT
Language        = English
The agent with name %1 has been already loaded. Please check the agent name in the configuration file.
.

MessageId       = +1
SymbolicName    = EVENT_UNCOMPATIBLE_AGENT
Language        = English
The agent %1 is not compatible with this bbwin version.
.

MessageId       = +1
SymbolicName    = EVENT_CANT_INSTANTIATE_AGENT
Language        = English
The agent %1 has been not instantiated. The reason was : %2
.

MessageId       = +1
SymbolicName    = EVENT_SERVICE_RELOAD
Language        = English
The configuration file has changed. BBWin is reloading the configuration.
.

MessageId       = +1
SymbolicName    = EVENT_SERVICE_STARTED
Language        = English
The service %1 has been successfully started. The hostname setting is set to "%2"
.

MessageId       = +1
SymbolicName    = EVENT_SERVICE_STOPPED
Language        = English
The service %1 has been successfully stopped.
.

MessageId       = +1
SymbolicName    = EVENT_MESSAGE_AGENT
Language        = English
The agent %1 generated this event message : %2
.

MessageId       = +1
SymbolicName    = EVENT_INVALID_TIMER
Language        = English
The personnalized timer for agent %1 is incorrect. Minimum value is 5 seconds and maximum value is 31 days. It will use default value %2 seconds
.

MessageId       = +1
SymbolicName    = EVENT_HOBBIT_MODE_NOT_SUPPORTED
Language        = English
The agent does not support hobbit centralized mode. It will be launched as a classical BBWin agent.
.

MessageId       = +1
SymbolicName    = EVENT_HOBBIT_FAILED_CLIENTDATA
Language        = English
BBWin failed to send the client data successfuly to the hobbit server. The error was : %1 : %2.
.

;////////////////////////////////////////
;// Additional messages
;//

;//MessageId       = 1000
;//SymbolicName    = IDS_HELLO
;//Language        = English
;//Hello World!
;//.

;
;#endif  //!__BBWINMESSAGES_H__
;
