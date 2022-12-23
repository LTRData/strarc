/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2022
 *
 * apifunc.cpp
 * Definitions for routines that publish strarc public API.
 */

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

// Use the WinStructured library classes and functions.
#include <windows.h>
#include <intsafe.h>
#include <shellapi.h>
#include <ntdll.h>

#include <winstrct.h>
#include <wio.h>
#include <wntsecur.h>
#include <wprocess.h>
#include <stdlib.h>

#include "strarc.hpp"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ntdll.lib")

