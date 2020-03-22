#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "sleep.h"

SYSTEM_INFO sysinfo = { 0 };

// This function gives away rest of current timeslice to other ready threads,
// if there is only one virtual processor in the system. Otherwise, this
// function does nothing.
void
YieldSingleProcessor()
{
  if (sysinfo.dwNumberOfProcessors == 0)
    GetSystemInfo(&sysinfo);

  if (sysinfo.dwNumberOfProcessors < 2)
    Sleep(0);
}

