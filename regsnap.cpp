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

#define WIN32_NO_STATUS
#include <windows.h>
#include <intsafe.h>
#undef WIN32_NO_STATUS
#include <ntdll.h>

#include <winstrct.h>
#include <wio.h>

#include "strarc.hpp"

bool
StrArc::CreateRegistrySnapshots()
{
    HKEY hKeyHiveList;
    LONG lRegErr;

    lRegErr = RegOpenKey(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\hivelist",
        &hKeyHiveList);

    if (lRegErr != ERROR_SUCCESS)
    {
        SetLastError(lRegErr);
        return false;
    }

    LPWSTR wczKeyName = (LPWSTR)Buffer;
    DWORD dwKeyNameSize = 32767;
    DWORD dwIndex = 0;

    for (;;)
    {
        YieldSingleProcessor();

        DWORD dwKeyNameReturnedSize = dwKeyNameSize;
        DWORD dwFileNameReturnedSize = FullPath.MaximumLength -
            sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION);
        DWORD dwReturnedDataType;

        lRegErr = RegEnumValue(hKeyHiveList,
            dwIndex++,
            wczKeyName,
            &dwKeyNameReturnedSize,
            NULL,
            &dwReturnedDataType,
            (LPBYTE)FullPath.Buffer,
            &dwFileNameReturnedSize);

        if (lRegErr == ERROR_NO_MORE_ITEMS)
        {
            RegCloseKey(hKeyHiveList);
            return true;
        }

        if (lRegErr != ERROR_SUCCESS)
        {
            SetLastError(lRegErr);
            return false;
        }

        if ((dwReturnedDataType != REG_SZ) | (lRegErr == ERROR_MORE_DATA))
        {
            oem_printf(stderr,
                "Wrong data type %1!u! for hive %2!ws!. "
                "Should be %3!u!. This hive will not be backed up.%%n",
                dwReturnedDataType, wczKeyName, REG_SZ);

            continue;
        }

        FullPath.Buffer[dwFileNameReturnedSize] = 0;
        FullPath.Length = (USHORT)(wcslen(FullPath.Buffer) << 1);

        if (FullPath.Length == 0)
            continue;

        NTSTATUS status =
            RtlAppendUnicodeToString(&FullPath, REGISTRY_SNAPSHOT_FILE_EXTENSION);
        if (!NT_SUCCESS(status))
        {
            oem_printf(stderr,
                "Registry file path is too long: '%1!wZ!'%%n"
                "Some of the snapshots will not be backed up.%%n",
                &FullPath);

            continue;
        }

        if (bVerbose)
            oem_printf(stderr,
                "'%1!ws!' -> '%2!wZ!'%%n",
                wczKeyName,
                &FullPath);
        else if (bListFiles)
            oem_printf(stdout, "%1!ws!%%n", wczKeyName);

        if (bListOnly)
            continue;

        UNICODE_STRING key_name;
        OBJECT_ATTRIBUTES object_attributes;
        HANDLE hKey;
        HANDLE hFile;

        RtlInitUnicodeString(&key_name, wczKeyName);
        InitializeObjectAttributes(&object_attributes,
            &key_name,
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
            WErrMsgA errmsg(RtlNtStatusToDosError(status));
            oem_printf(stderr, "Error opening key '%1!ws!': %2%%n",
                wczKeyName, errmsg);

            continue;
        }

        hFile =
            NativeCreateFile(NULL,
                &FullPath,
                GENERIC_WRITE,
                OBJ_CASE_INSENSITIVE,
                NULL,
                NULL,
                FILE_ATTRIBUTE_NORMAL,
                0,
                FILE_SUPERSEDE,
                0,
                FALSE);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            WErrMsgA errmsg;

            NtClose(hKey);

            oem_printf(stderr,
                "Error creating file '%2!wZ!': %1%%n",
                errmsg,
                &FullPath);

            continue;
        }

        status = NtSaveKey(hKey, hFile);

        NtClose(hKey);
        NtClose(hFile);

        if (!NT_SUCCESS(status))
        {
            WErrMsgA errmsg(RtlNtStatusToDosError(status));
            oem_printf(stderr, "Error creating snapshot for '%1!ws!': %2%%n",
                wczKeyName, errmsg);

            continue;
        }
    }
}
