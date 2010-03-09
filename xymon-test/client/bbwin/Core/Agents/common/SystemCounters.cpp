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

// this code has been done with the help of Dudi Avramov ( Author of the PerfCounter.h)

#include <atlbase.h>	// for CRegKey use
#include "SystemCounters.h"

#pragma pack(push,8)
#include "PerfCounters.h"
#pragma pack(pop)

#define SYSTEM_OBJECT_INDEX					2		// 'System' object
#define SYSTEM_UPTIME_COUNTER_INDEX			674		// 'System up time

#define SERVER_OBJECT_INDEX					330		// 'Server' object
#define SERVER_SESSIONS_OBJECT_INDEX		314		// 'Server Session



CSystemCounters::CSystemCounters()
{
}

CSystemCounters::~CSystemCounters()
{
}

///////////////////////////////////////////////////////////////////
//
//		GetSystemUpTime uses the performance counters to retrieve the
//		system up time.
//		The system up time counter is of type PERF_ELAPSED_TIME
//		which as the following calculation:
//
//		Element		Value
//		=======		===========
//		X			CounterData
//		Y			PerfTime
//		Data Size	8 Bytes
//		Time base	PerfFreq
//		Calculation (Y-X)/TB
//
///////////////////////////////////////////////////////////////////
DWORD CSystemCounters::GetSystemUpTime()
{
	CPerfCounters<LONGLONG> PerfCounters;
	DWORD dwObjectIndex = SYSTEM_OBJECT_INDEX;
	DWORD dwSystemUpTimeCounter = SYSTEM_UPTIME_COUNTER_INDEX;

	PPERF_DATA_BLOCK pPerfData = NULL;
	PPERF_OBJECT_TYPE pPerfObj = NULL;
	LONGLONG x = PerfCounters.GetCounterValue(&pPerfData, &pPerfObj, dwObjectIndex, dwSystemUpTimeCounter, NULL);
	LONGLONG y = pPerfObj->PerfTime.QuadPart;
	LONGLONG tb = pPerfObj->PerfFreq.QuadPart;

	if (tb == 0)
		return 0;
	DWORD UpTime = (DWORD)((y-x) / tb);
	if (UpTime < 0)
		return 0;

	return UpTime;
}


DWORD CSystemCounters::GetServerSessions()
{
	CPerfCounters<LONGLONG> PerfCounters;
	DWORD dwObjectIndex = SERVER_OBJECT_INDEX;
	DWORD dwServerSessionsCounter = SERVER_SESSIONS_OBJECT_INDEX;

	PPERF_DATA_BLOCK pPerfData = NULL;
	PPERF_OBJECT_TYPE pPerfObj = NULL;
	LONGLONG x = PerfCounters.GetCounterValue(&pPerfData, &pPerfObj, dwObjectIndex, dwServerSessionsCounter, NULL);
	return (DWORD)x;
}
