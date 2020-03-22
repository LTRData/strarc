/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2013
 *
 * constnam.cpp
 * Human readable names of backup-related constants.
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
#include <winstrct.h>
#include <shellapi.h>

#include "sleep.h"

#include <wfind.h>
#include <wio.h>
#include <wntsecur.h>
#include <wprocess.h>
#include <stdlib.h>

#include "strarc.hpp"

// Link the .exe file to CRTDLL.DLL. This makes it run without additional DLL
// files even on very old versions of Windows NT.
#ifdef _WIN64
#pragma comment(lib, "msvcrt.lib")
#else
// crthlp.lib is only needed when x86 version is built with 14.00 and later
// versions of MSVC++ compiler
//#pragma comment(lib, "crthlp.lib")
#pragma comment(lib, "crtdll.lib")
// The WinStructured lib files are usually marked for linking with msvcrt.lib.
#pragma comment(linker, "/nodefaultlib:msvcrt.lib")
#endif

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ntdll.lib")

char *stream_ids[] = {
  "BACKUP_INVALID",        // 0x00000000 Header (not valid backup stream) 
  "BACKUP_DATA",           // 0x00000001 Standard data 
  "BACKUP_EA_DATA",        // 0x00000002 Extended attribute data 
  "BACKUP_SECURITY_DATA",  // 0x00000003 Security descriptor data 
  "BACKUP_ALTERNATE_DATA", // 0x00000004 Alternative data streams 
  "BACKUP_LINK",           // 0x00000005 Hard link information 
  "BACKUP_PROPERTY_DATA",  // 0x00000006 Property data 
  "BACKUP_OBJECT_ID",      // 0x00000007 Objects identifiers 
  "BACKUP_REPARSE_DATA",   // 0x00000008 Reparse points 
  "BACKUP_SPARSE_BLOCK",   // 0x00000009 Sparse file. 
  "BACKUP_TXFS_DATA"       // 0x0000000a TXFS stream
};

char stream_id_unknown[12];

char *GetStreamIdDescription(DWORD StreamId)
{
  if (StreamId >= (sizeof(stream_ids) / sizeof(*stream_ids)))
    {
      _snprintf(stream_id_unknown, sizeof(stream_id_unknown),
		"%#x", StreamId);
      stream_id_unknown[sizeof(stream_id_unknown)-1] = 0;
      return stream_id_unknown;
    }

  return stream_ids[StreamId];
}

char *attrib_ids[] = {
  "STREAM_NORMAL_ATTRIBUTE", //         0x00000000
  "STREAM_MODIFIED_WHEN_READ", //       0x00000001
  "STREAM_CONTAINS_SECURITY", //        0x00000002
  "STREAM_CONTAINS_PROPERTIES", //      0x00000004
  "STREAM_SPARSE_ATTRIBUTE" //          0x00000008
};

char attrib_id_list[260];

char *GetStreamAttributesDescription(DWORD StreamAttributesId)
{
  if (StreamAttributesId == 0)
    return attrib_ids[0];

  attrib_id_list[0] = 0;

  for (int i = 1;
       (i < (sizeof(attrib_ids) / sizeof(*attrib_ids))) &
	 (StreamAttributesId != 0);
       i++, StreamAttributesId >>= 1)
    if (StreamAttributesId & 1)
      {
	if (attrib_id_list[0] != 0)
	  {
	    strncat(attrib_id_list, " | ", sizeof(attrib_id_list));
	    attrib_id_list[sizeof(attrib_id_list)-1] = 0;
	  }

	strncat(attrib_id_list, attrib_ids[i], sizeof(attrib_id_list));
	attrib_id_list[sizeof(attrib_id_list)-1] = 0;
      }

  return attrib_id_list;
}
