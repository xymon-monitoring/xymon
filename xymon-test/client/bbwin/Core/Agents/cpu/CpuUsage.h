// Code by  By Dudi Avramov  
// http://www.codeproject.com/system/cpuusage.asp
// it replace the need of pdh.lib
// use wstring instead of bstr_t by etienne grignon
// autorization of use confirmed by email 


#ifndef _CPUUSAGE_H
#define _CPUUSAGE_H

#include <windows.h>

#define TOTAL_CPU_USAGE_PROC		1024

class CCpuUsage
{
public:
	CCpuUsage();
	virtual ~CCpuUsage();

// Methods
	double GetCpuUsage();
	double GetCpuUsage(DWORD dwProcessID);

	BOOL EnablePerformaceCounters(BOOL bEnable = TRUE);
	void	SetProcessorIndex(DWORD index) { m_proc = index; };

// Attributes
private:
	bool			m_bFirstTime;
	LONGLONG		m_lnOldValue ;
	LARGE_INTEGER	m_OldPerfTime100nSec;
	DWORD			m_proc;
};


#endif