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


#ifndef		__PROCAPI_H__
#define		__PROCAPI_H__

typedef LONG    NTSTATUS;
typedef LONG    KPRIORITY;

#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

#define STATUS_INFO_LENGTH_MISMATCH      ((NTSTATUS)0xC0000004L)

#define SystemProcessesAndThreadsInformation    5

typedef struct _CLIENT_ID {
    DWORD         UniqueProcess;
    DWORD         UniqueThread;
} CLIENT_ID;

typedef struct _UNICODE_STRING {
    USHORT        Length;
    USHORT        MaximumLength;
    PWSTR         Buffer;
} UNICODE_STRING;

typedef struct _VM_COUNTERS {
    SIZE_T        PeakVirtualSize;
    SIZE_T        VirtualSize;
    ULONG         PageFaultCount;
    SIZE_T        PeakWorkingSetSize;
    SIZE_T        WorkingSetSize;
    SIZE_T        QuotaPeakPagedPoolUsage;
    SIZE_T        QuotaPagedPoolUsage;
    SIZE_T        QuotaPeakNonPagedPoolUsage;
    SIZE_T        QuotaNonPagedPoolUsage;
    SIZE_T        PagefileUsage;
    SIZE_T        PeakPagefileUsage;
} VM_COUNTERS;

typedef struct _SYSTEM_THREAD_INFORMATION {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG         WaitTime;
    PVOID         StartAddress;
    CLIENT_ID     ClientId;
    KPRIORITY     Priority;
    KPRIORITY     BasePriority;
    ULONG         ContextSwitchCount;
    LONG          State;
    LONG          WaitReason;
} SYSTEM_THREAD_INFORMATION, * PSYSTEM_THREAD_INFORMATION;

typedef struct _SYSTEM_PROCESS_INFORMATION {
    ULONG             NextEntryDelta;
    ULONG             ThreadCount;
    ULONG             Reserved1[6];
    LARGE_INTEGER     CreateTime;
    LARGE_INTEGER     UserTime;
    LARGE_INTEGER     KernelTime;
    UNICODE_STRING    ProcessName;
    KPRIORITY         BasePriority;
    ULONG             ProcessId;
    ULONG             InheritedFromProcessId;
    ULONG             HandleCount;
    ULONG             Reserved2[2];
    VM_COUNTERS       VmCounters;
#if _WIN32_WINNT >= 0x500
    IO_COUNTERS       IoCounters;
#endif
    SYSTEM_THREAD_INFORMATION Threads[1];
} SYSTEM_PROCESS_INFORMATION, * PSYSTEM_PROCESS_INFORMATION;



#endif // !__PROCAPI_H__

