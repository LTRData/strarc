#ifndef _DLL
#define _DLL
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ntdll.h>
#include <intsafe.h>
#include <winstrct.h>

#include "ntfind.hpp"

#ifdef _WIN64
#pragma comment(lib, "msvcrt.lib")
#elif defined(_DLL)
#pragma comment(lib, "minwcrt.lib")
#pragma comment(linker, "/nodefaultlib:msvcrt.lib")
#endif
#pragma comment(lib, "ntdll.lib")

BOOL
BackupFile(HANDLE RootDirectory,
	   PUNICODE_STRING parent_name,
	   PUNICODE_STRING entry_name,
	   PUNICODE_STRING entry_short_name,
	   ULONG ObjectOpenAttributes);

BOOL
BackupHandleAsName(HANDLE RootDirectory,
		   HANDLE handle,
		   PUNICODE_STRING archive_name,
		   PUNICODE_STRING short_name);

int
wmain(int argc, LPWSTR *argv)
{
  SetOemPrintFLineLength(GetStdHandle(STD_ERROR_HANDLE));

  WCHAR Buffer[32767] = { 0 };
  UNICODE_STRING FullPath =
    {
      0,
      sizeof(Buffer),
      Buffer
    };

  HANDLE root_dir_handle;

  if ((argc > 1) ?
      (wcsncmp(argv[1], L"-d:", 3) == 0) : false)
    {
      LPWSTR root_dir = argv[1] + 3;
      --argc;
      ++argv;

      UNICODE_STRING root_dir_name;
      RtlDosPathNameToNtPathName_U(root_dir, &root_dir_name, NULL, NULL);

      oem_printf(stderr,
		 "Root dir is: '%1!.*ws!'%%n",
		 root_dir_name.Length >> 1, root_dir_name.Buffer);

      root_dir_handle =
	NativeOpenFile(NULL,
		       &root_dir_name,
		       GENERIC_READ,
		       OBJ_CASE_INSENSITIVE,
		       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		       FILE_OPEN_FOR_BACKUP_INTENT);

      RtlFreeUnicodeString(&root_dir_name);

      if (root_dir_handle == INVALID_HANDLE_VALUE)
	{
	  WErrMsg errmsg;
	  oem_printf(stderr, "Error opening root directory: %1!ws!%%n",
		     (LPWSTR)errmsg);
	  return 1;
	}
    }
  else
    root_dir_handle = NtCurrentDirectoryHandle();

  WCHAR *arg_array[] = { L"", L"." };
  if (argc == 1)
    {
      argv = arg_array;
      argc = 2;
    }

  while (--argc > 0)
    {
      UNICODE_STRING entry_name;
      RtlInitUnicodeString(&entry_name, *++argv);

      UNICODE_STRING entry_short_name = { 0 };

      if (!BackupFile(root_dir_handle,
		      &FullPath,
		      &entry_name,
		      &entry_short_name,
		      OBJ_CASE_INSENSITIVE))
	{
	  WErrMsg errmsg;
	  oem_printf(stderr, "Error backing up '%2!.*ws!': %1!ws!%%n",
		     (LPWSTR)errmsg,
		     entry_name.Length >> 1, entry_name.Buffer);
	}
    }

  NtClose(root_dir_handle);

  return 0;
}

BOOL
BackupFile(HANDLE RootDirectory,
	   PUNICODE_STRING parent_name,
	   PUNICODE_STRING entry_name,
	   PUNICODE_STRING entry_short_name,
	   ULONG ObjectOpenAttributes)
{
  UNICODE_STRING name = *parent_name;

  NTSTATUS status = STATUS_SUCCESS;

  if (name.Length > 0)
    status = RtlAppendUnicodeToString(&name, L"\\");

  if (NT_SUCCESS(status) &&
      !((entry_name->Length == 2) & (entry_name->Buffer[0] == L'.')))
    status = RtlAppendUnicodeStringToString(&name, entry_name);

  if (!NT_SUCCESS(status))
    {
      if (status == STATUS_BUFFER_TOO_SMALL)
	SetLastError(ERROR_BAD_PATHNAME);
      else
	SetLastError(RtlNtStatusToDosError(status));

      return FALSE;
    }

  HANDLE handle =
    NativeOpenFile(RootDirectory,
		   &name,
		   GENERIC_READ,
		   ObjectOpenAttributes,
		   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		   FILE_OPEN_FOR_BACKUP_INTENT);

  if (handle == INVALID_HANDLE_VALUE)
    return FALSE;

  BOOL result =
    BackupHandleAsName(RootDirectory, handle, &name, entry_short_name);

  NtClose(handle);

  return result;
}

BOOL
BackupHandleAsName(HANDLE RootDirectory,
		   HANDLE handle,
		   PUNICODE_STRING archive_name,
		   PUNICODE_STRING short_name)
{
  BY_HANDLE_FILE_INFORMATION file_info;

  if (!GetFileInformationByHandle(handle, &file_info))
    return FALSE;

  if (file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      for (NtFileFinder finddata(handle); ; finddata.Next())
	{
	  if (!finddata)
	    {
	      DWORD dwLastError = finddata.GetLastError();
	      if (dwLastError == ERROR_NO_MORE_FILES)
		break;
	      else
		{
		  WErrMsg errmsg(dwLastError);
		  oem_printf(stderr,
			     "NtFileFinder failed: %1!ws!%%n",
			     (LPWSTR) errmsg);
		  SetLastError(dwLastError);
		  return FALSE;
		}
	    }

	  if ((finddata.FileNameLength == 2) &
	      (finddata.FileName[0] == L'.'))
	    continue;
	  if ((finddata.FileNameLength == 4) &
	      (finddata.FileName[0] == L'.') &
	      (finddata.FileName[1] == L'.'))
	    continue;

	  if (finddata.FileNameLength > USHORT_MAX)
	    {
	      WErrMsg errmsg(ERROR_BAD_PATHNAME);
	      oem_printf(stderr, "Error '%3!.*ws!%2%5!.*ws!': %1!ws!%%n",
			 (LPWSTR)errmsg,
			 archive_name->Length > 0 ? "\\" : "",
			 archive_name->Length >> 1, archive_name->Buffer,
			 finddata.FileNameLength >> 1, finddata.FileName,
			 finddata.FileNameLength >> 1, finddata.FileName);
	      continue;
	    }

	  USHORT FileNameLength = (USHORT)finddata.FileNameLength;

	  UNICODE_STRING entry_name =
	    {
	      FileNameLength,
	      FileNameLength,
	      finddata.FileName
	    };

	  if (finddata.ShortNameLength > USHORT_MAX)
	    {
	      WErrMsg errmsg(ERROR_BAD_PATHNAME);
	      oem_printf(stderr, "Error '%3!.*ws!%2%5!.*ws!': %1!ws!%%n",
			 (LPWSTR)errmsg,
			 archive_name->Length >> 1, archive_name->Buffer,
			 archive_name->Length > 0 ? "\\" : "",
			 finddata.ShortNameLength >> 1, finddata.ShortName,
			 finddata.ShortNameLength >> 1, finddata.ShortName);
	      continue;
	    }

	  USHORT ShortNameLength = (USHORT)finddata.ShortNameLength;

	  UNICODE_STRING entry_short_name =
	    {
	      ShortNameLength,
	      ShortNameLength,
	      finddata.ShortName
	    };

	  if (!BackupFile(RootDirectory,
			  archive_name,
			  &entry_name,
			  &entry_short_name,
			  0))
	    {
	      WErrMsg errmsg(ERROR_BAD_PATHNAME);
	      oem_printf(stderr, "Error '%3!.*ws!%2%5!.*ws!': %1!ws!%%n",
			 (LPWSTR)errmsg,
			 archive_name->Length > 0 ? "\\" : "",
			 archive_name->Length >> 1, archive_name->Buffer,
			 finddata.FileNameLength >> 1, finddata.FileName,
			 finddata.FileNameLength >> 1, finddata.FileName);
	      continue;
	    }
	}
    }

  if (archive_name->Length == 0)
    RtlInitUnicodeString(archive_name, L".");

  RtlAppendUnicodeToString(archive_name, L"\r\n");
  DWORD dw;
  WriteFile(hStdOut, archive_name->Buffer, archive_name->Length, &dw, NULL);
  short_name;

  return TRUE;
}
