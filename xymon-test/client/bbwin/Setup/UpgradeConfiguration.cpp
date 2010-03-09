#include <windows.h>
#include <msi.h>
#include <msiquery.h>

#pragma comment(linker, "/EXPORT:UpgradeConfiguration=_UpgradeConfiguration@4")

extern "C" UINT __stdcall UpgradeConfiguration (MSIHANDLE hInstall) {
  //char conf[MAX_PATH];
  //DWORD Len = MAX_PATH;

  //MsiGetProperty (hInstall, "PIDKEY", Pid, &PidLen);
  // MsiSetProperty (hInstall, "PIDACCEPTED", Pid[0] == '1' ? "1" : "0");
  return ERROR_SUCCESS;
} // UpgradeConfiguration
