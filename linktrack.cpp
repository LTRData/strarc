#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _DLL
#define _DLL
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stdio.h>

#include "strarc.hpp"
#include "linktrack.hpp"

#pragma comment(lib, "crthlp.lib")
#pragma comment(lib, "crtdll.lib")

LinkInfo *LinkTracker[256] = { 0 };
