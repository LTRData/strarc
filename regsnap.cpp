/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2008
 *
 * regsnap.cpp
 * Creates registry database snapshots.
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

#include <windows.h>
#include <ntdll.h>
#include <winstrct.h>

#include "strarc.hpp"

#ifdef _WIN64
#pragma comment(lib, "msvcrt.lib")
#else
#pragma comment(lib, "crthlp.lib")
#pragma comment(lib, "crtdll.lib")
#endif
#pragma comment(lib, "ntdll.lib")

void
CreateRegistrySnapshots()
{
  HKEY hKeyHiveList;
  LONG lRegErr;

  lRegErr = RegOpenKey(HKEY_LOCAL_MACHINE,
		       L"SYSTEM\\CurrentControlSet\\Control\\hivelist",
		       &hKeyHiveList);

  if (lRegErr != ERROR_SUCCESS)
    {
      WErrMsgA errmsg(lRegErr);
      oem_printf(stderr,
		 "Error getting registry hive list: %1%%n"
		 "No registry snapshots will be backed up.%%n", errmsg);
      return;
    }

  LPWSTR wczKeyName = (LPWSTR) Buffer;
  DWORD dwKeyNameSize = 32767;
  LPWSTR wczFileName = wczFullPathBuffer;
  DWORD dwFileNameSize =
    sizeof(wczFullPathBuffer) - sizeof(*wczFullPathBuffer);
  DWORD dwIndex = 0;

  for(;;)
    {
      Sleep(0);

      DWORD dwKeyNameReturnedSize = dwKeyNameSize;
      DWORD dwFileNameReturnedSize = dwFileNameSize -
	sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION);
      DWORD dwReturnedDataType;

      lRegErr = RegEnumValue(hKeyHiveList,
			     dwIndex++,
			     wczKeyName,
			     &dwKeyNameReturnedSize,
			     NULL,
			     &dwReturnedDataType,
			     (LPBYTE) wczFileName,
			     &dwFileNameReturnedSize);

      if (lRegErr == ERROR_NO_MORE_ITEMS)
	break;

      if ((lRegErr != ERROR_SUCCESS) & (lRegErr != ERROR_MORE_DATA))
	{
	  WErrMsgA errmsg(lRegErr);
	  oem_printf(stderr,
		     "Error getting registry hive list: %1%%n"
		     "Some of the snapshots will not be backed up.%%n",
		     errmsg);

	  break;
	}

      if (dwReturnedDataType != REG_SZ)
	{
	  oem_printf(stderr,
		     "Wrong data type %1!u! for hive %2!ws!. "
		     "Should be %3!u!. This hive will not be backed up.%%n",
		     dwReturnedDataType, wczKeyName, REG_SZ);

	  continue;
	}

      if (*wczFileName == 0)
	continue;

      wcscat(wczFileName, REGISTRY_SNAPSHOT_FILE_EXTENSION);

      if (bVerbose)
	oem_printf(stderr, "'%1!ws!' -> '%2!ws!'%%n", wczKeyName, wczFileName);
      else if (bListFiles)
	oem_printf(stdout, "%1!ws!%%n", wczKeyName);

      NTSTATUS status;
      IO_STATUS_BLOCK io_status;
      UNICODE_STRING name;
      OBJECT_ATTRIBUTES object_attributes;
      HANDLE hKey;
      HANDLE hFile;

      RtlInitUnicodeString(&name, wczKeyName);
      InitializeObjectAttributes(&object_attributes,
				 &name,
				 OBJ_CASE_INSENSITIVE | OBJ_OPENIF,
				 NULL,
				 NULL);

      status = NtCreateKey(&hKey,
			   KEY_READ,
			   &object_attributes,
			   0,
			   NULL,
			   REG_OPTION_BACKUP_RESTORE,
			   NULL);

      if (!NT_SUCCESS(status))
	{
	  WNTErrMsgA errmsg(status);
	  oem_printf(stderr, "Error opening key '%1!ws!': %2%%n",
		     wczKeyName, errmsg);

	  continue;
	}

      RtlInitUnicodeString(&name, wczFileName);
      InitializeObjectAttributes(&object_attributes,
				 &name,
				 OBJ_CASE_INSENSITIVE | OBJ_OPENIF,
				 NULL,
				 NULL);

      status = NtCreateFile(&hFile,
			    GENERIC_WRITE | SYNCHRONIZE,
			    &object_attributes,
			    &io_status,
			    NULL,
			    FILE_ATTRIBUTE_NORMAL,
			    0,
			    FILE_SUPERSEDE,
			    FILE_NON_DIRECTORY_FILE |
			    FILE_SYNCHRONOUS_IO_NONALERT,
			    NULL,
			    0);

      if (!NT_SUCCESS(status))
	{
	  NtClose(hKey);

	  WNTErrMsgA errmsg(status);
	  oem_printf(stderr, "Error creating file '%1!ws!': %2%%n",
		     wczFileName, errmsg);

	  continue;
	}

      status = NtSaveKey(hKey, hFile);

      NtClose(hKey);
      NtClose(hFile);

      if (!NT_SUCCESS(status))
	{
	  WNTErrMsgA errmsg(status);
	  oem_printf(stderr, "Error creating snapshot for '%1!ws!': %2%%n",
		     wczKeyName, errmsg);

	  continue;
	}
    }

  RegCloseKey(hKeyHiveList);
}
