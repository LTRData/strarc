/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2013
 *
 * restore.cpp
 * Restore operation source file.
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
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x500
#endif

#include <windows.h>
#include <ntdll.h>
#include <intsafe.h>
#include <winstrct.h>

#include <winstrct.h>
#include <winioctl.h>

#include "sleep.h"
#include "ntfind.hpp"

#include <wfind.h>

#include "strarc.hpp"
#include "lnk.h"

#ifndef _WIN64
#pragma comment(lib, "crthlp.lib")
#pragma comment(lib, "crtdll.lib")
#endif

bool
IsNewFileHeader()
{
  if ((header.dwStreamId == BACKUP_INVALID) &&
      (header.dwStreamAttributes == STRARC_MAGIC) &&
      ((header.Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION) |
       (header.Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION + 26))
      && (header.dwStreamNameSize > 0)
      && (header.dwStreamNameSize <= 65535))
    return true;

  if (header.dwStreamNameSize & 1)
    return true;

  return false;
}

void
FillEntireBuffer(DWORD &dwBytesRead, PULARGE_INTEGER BytesToRead)
{
  if (bVerbose)
    fprintf(stderr, ", [id=%s, attr=%s, size=0x%.8x%.8x",
	    GetStreamIdDescription(header.dwStreamId),
	    GetStreamAttributesDescription(header.dwStreamAttributes),
	    header.Size.HighPart, header.Size.LowPart);

  BytesToRead->QuadPart = header.dwStreamNameSize + header.Size.QuadPart;

  DWORD FirstBlockDataToRead =
    (BytesToRead->QuadPart > (DWORDLONG) (dwBufferSize - HEADER_SIZE)) ?
    dwBufferSize - HEADER_SIZE : BytesToRead->LowPart;

  if (FirstBlockDataToRead > 0)
    {
      DWORD FirstBlockDataDone =
	ReadArchive(Buffer + HEADER_SIZE, FirstBlockDataToRead);

      BytesToRead->QuadPart -= FirstBlockDataDone;

      if (FirstBlockDataDone != FirstBlockDataToRead)
	{
	  if (bVerbose)
	    fprintf(stderr, ", %.4g %s missing.\r\n",
		    TO_h(BytesToRead->QuadPart),
		    TO_p(BytesToRead->QuadPart));
	  status_exit(XE_ARCHIVE_TRUNC);
	}

      dwBytesRead += FirstBlockDataDone;
    }

  if (bVerbose)
    {
      if ((header.dwStreamId == BACKUP_SPARSE_BLOCK) &
	  (header.dwStreamAttributes == STREAM_SPARSE_ATTRIBUTE) &
	  (header.Size.QuadPart >= sizeof(LARGE_INTEGER)))
	{
	  PLARGE_INTEGER StartPosition = (PLARGE_INTEGER)
	    (Buffer + HEADER_SIZE + header.dwStreamNameSize);

	  fprintf(stderr, ", stream='%.*ws', offset=0x%.8x%.8x]",
		  header.dwStreamNameSize >> 1,
		  header.cStreamName,
		  StartPosition->HighPart, StartPosition->LowPart);
	}
      else
	fprintf(stderr, ", name='%.*ws']",
		header.dwStreamNameSize >> 1,
		header.cStreamName);
    }
}

bool
WriteHardLinkFromArchive(HANDLE &hFile,
			 DWORD dwBytesRead,
			 LPCWSTR wczFullPath,
			 LPCWSTR wczFile,
			 bool bSeekOnly)
{
  NativeDeleteFile(hFile);
  CloseHandle(hFile);
  hFile = INVALID_HANDLE_VALUE;

  DWORD offset = HEADER_SIZE + header.dwStreamNameSize;
  LPWSTR LinkName = (LPWSTR) (Buffer + offset);

  // If relative name is longer than read buffer, that is an archive
  // format error. Relative name should fit in one buffered read.
  if (dwBytesRead != (header.Size.QuadPart + offset))
    {
      if (bVerbose)
	oem_printf(stderr,
		   " incomplete stream: Possibly corrupt or "
		   "truncated archive.%%n"
		   "The link '%1!ws!' will not be restored.%%n",
		   wczFullPath);
      else
	oem_printf(stderr, "strarc: Error in archive when extracting "
		   "%1!ws!%%n", wczFullPath);

      bSeekOnly = true;
    }

  if (!bSeekOnly)
    {
      LinkName[header.Size.LowPart >> 1] = 0;

      if (bVerbose)
	oem_printf(stderr, ", link '%1!ws!'", LinkName);
    }

  if (bHardLinkSupport & !bSeekOnly)
    {
      hFile = CreateFile(LinkName, 0,
			 FILE_SHARE_READ | FILE_SHARE_WRITE |
			 FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			 FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (hFile == INVALID_HANDLE_VALUE)
	{
	  WErrMsgA errmsg;
	  oem_printf(stderr,
		     "strarc: Cannot open '%1!ws!' to restore link: "
		     "%2%%n"
		     "The link '%3!ws!' will not be restored.%%n",
		     LinkName, errmsg, wczFile);
	  bSeekOnly = true;
	}
    }

  if (!bSeekOnly)
    if (bHardLinkSupport ?
	!CreateHardLinkToOpenFile(hFile, wczFile, TRUE) : true)
      {
	if (bVerbose)
	  if (bHardLinkSupport)
	    {
	      WErrMsgA errmsg;
	      oem_printf(stderr, ", link failed: %1%%n"
			 "Copying instead",
			 errmsg);
	    }
	  else
	    oem_printf(stderr, ", copying to '%1!ws!'", wczFile);
	else
	  if (bHardLinkSupport)
	    {
	      WErrMsgA errmsg;
	      oem_printf(stderr,
			 "Failed to create hard link '%1!ws!': "
			 "%2%%n"
			 "Copying '%3!ws!' instead.%%n",
			 wczFile, errmsg, LinkName);
	    }

	if (hFile != INVALID_HANDLE_VALUE)
	  {
	    CloseHandle(hFile);
	    hFile = INVALID_HANDLE_VALUE;
	  }
	if (!CopyFile(LinkName, wczFile, FALSE))
	  {
	    WErrMsgA errmsg;
	    oem_printf(stderr,
		       "strarc: %1%%n"
		       "Failed both linking and copying '%2!ws!' to "
		       "'%3!ws!'.%%n",
		       errmsg, LinkName, wczFile);
	    DeleteFile(wczFile);
	    bSeekOnly = true;
	  }

	if (!bSeekOnly)
	  {
	    hFile = CreateFile(wczFile,
			       GENERIC_READ | GENERIC_WRITE |
			       WRITE_DAC | WRITE_OWNER | DELETE |
			       ACCESS_SYSTEM_SECURITY,
			       FILE_SHARE_READ | FILE_SHARE_DELETE,
			       NULL, OPEN_EXISTING,
			       FILE_ATTRIBUTE_NORMAL |
			       FILE_FLAG_BACKUP_SEMANTICS, NULL);
	    if (hFile == INVALID_HANDLE_VALUE)
	      {
		WErrMsgA errmsg;
		oem_printf(stderr,
			   "strarc: Cannot open '%1!ws!': %2%%n",
			   wczFile, errmsg);
		bSeekOnly = true;
	      }
	  }
      }
    else
      {
	CloseHandle(hFile);
	hFile = INVALID_HANDLE_VALUE;
      }

  return true;
}

bool
WriteFileFromArchive(HANDLE &hFile,
		     DWORD dwBytesRead,
		     PULARGE_INTEGER BytesToRead,
		     UNICODE_STRING FullPath,
		     bool &bSeekOnly)
{
  LPVOID lpCtx = NULL;

  if ((!bSeekOnly) &
      (header.dwStreamId == BACKUP_SPARSE_BLOCK) &
      (header.dwStreamAttributes == STREAM_SPARSE_ATTRIBUTE))
    {
      PLARGE_INTEGER StartPosition = (PLARGE_INTEGER)
	(Buffer + HEADER_SIZE + header.dwStreamNameSize);

      if ((SetFilePointer(hFile, StartPosition->LowPart,
			  &StartPosition->HighPart, FILE_BEGIN) ==
	   INVALID_SET_FILE_POINTER) ?
	  (GetLastError() != NO_ERROR) : false)
	{
	  WErrMsgA errmsg;
	  oem_printf(stderr,
		     "strarc: Seek error in '%2!.*ws!': %1%%n",
		     errmsg,
		     FullPath.Length >> 1,
		     FullPath.Buffer);
	  bSeekOnly = true;
	}

      if (!SetEndOfFile(hFile))
	{
	  WErrMsgA errmsg;
	  oem_printf(stderr,
		     "strarc: Size error in '%2!.*ws!': %1%%n",
		     errmsg,
		     FullPath.Length >> 1,
		     FullPath.Buffer);
	  bSeekOnly = true;
	}
    }

  if (!bSeekOnly)
    if (!BackupWrite(hFile, Buffer, dwBytesRead,
		     &dwBytesRead, FALSE, bProcessSecurity, &lpCtx))
      {
	WErrMsgA errmsg;
	oem_printf(stderr,
		   "strarc: Cannot write '%2!.*ws!': %1%%n",
		   errmsg,
		   FullPath.Length >> 1,
		   FullPath.Buffer);
	bSeekOnly = true;
	NativeDeleteFile(hFile);
	CloseHandle(hFile);
	hFile = INVALID_HANDLE_VALUE;
      }

  while (BytesToRead->QuadPart > 0)
    {
      YieldSingleProcessor();

      if (bCancel)
	{
	  if (hFile != INVALID_HANDLE_VALUE)
	    {
	      NativeDeleteFile(hFile);
	      CloseHandle(hFile);
	    }
	  return false;
	}

      dwBytesRead = ReadArchive(Buffer,
				BytesToRead->QuadPart >
				(DWORDLONG) dwBufferSize ?
				dwBufferSize : BytesToRead->LowPart);

      BytesToRead->QuadPart -= dwBytesRead;

      if (dwBytesRead == 0)
	{
	  if (bVerbose)
	    fprintf(stderr, ", %.4g %s missing.\r\n",
		    TO_h(BytesToRead->QuadPart),
		    TO_p(BytesToRead->QuadPart));
	  status_exit(XE_ARCHIVE_TRUNC);
	}

      if (!bSeekOnly)
	if (!BackupWrite(hFile, Buffer, dwBytesRead, &dwBytesRead, FALSE,
			 bProcessSecurity, &lpCtx))
	  {
	    WErrMsgA errmsg;
	    oem_printf(stderr,
		       "strarc: Restore of '%2!.*ws!' incomplete: %1%%n",
		       errmsg,
		       FullPath.Length >> 1,
		       FullPath.Buffer);

	    if (lpCtx != NULL)
	      BackupWrite(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);

	    if (hFile != INVALID_HANDLE_VALUE)
	      {
		NativeDeleteFile(hFile);
		CloseHandle(hFile);
	      }

	    return false;
	  }
    }

  if (lpCtx != NULL)
    BackupWrite(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);

  return true;
}

bool
RestoreAlternateStream(DWORD dwBytesRead,
		       PULARGE_INTEGER BytesToRead,
		       ULONG StreamAttributes,
		       PUNICODE_STRING FileBaseName,
		       bool bSeekOnly)
{
  for (;;)
    {
      UNICODE_STRING complete_name = *FileBaseName;

      UNICODE_STRING stream_name;
      InitCountedUnicodeString(&stream_name,
			       header.cStreamName,
			       (USHORT) header.dwStreamNameSize);

      NTSTATUS status =
	RtlAppendUnicodeStringToString(&complete_name,
				       &stream_name);

      if (!NT_SUCCESS(status))
	{
	  oem_printf(stderr,
		     "strarc: Bad path name '%1!.*ws!:%3!.*ws!%%n",
		     FileBaseName->Length >> 1, FileBaseName->Buffer,
		     stream_name.Length >> 1, stream_name.Buffer,
		     stream_name.Length >> 1, stream_name.Buffer);

	  bSeekOnly;
	}
	
      HANDLE hStream =
	NativeCreateFile(RootDirectory,
			 &complete_name,
			 GENERIC_READ | GENERIC_WRITE | WRITE_DAC |
			 WRITE_OWNER | DELETE | ACCESS_SYSTEM_SECURITY,
			 0,
			 NULL,
			 NULL,
			 StreamAttributes,
			 FILE_SHARE_READ |
			 FILE_SHARE_DELETE,
			 FILE_SUPERSEDE,
			 FILE_OPEN_FOR_BACKUP_INTENT |
			 FILE_NON_DIRECTORY_FILE |
			 FILE_RANDOM_ACCESS);

      if (hStream == INVALID_HANDLE_VALUE)
	{
	  WErrMsgA errmsg;
	  oem_printf(stderr,
		     "Error restoring stream '%2!.*ws!': %1%%n",
		     errmsg,
		     complete_name.Length >> 1,
		     complete_name.Buffer);

	  bSeekOnly = true;
	}

      if ((!bSeekOnly) &&
	  bRestoreCompression &&
	  (header.dwStreamAttributes & STREAM_SPARSE_ATTRIBUTE))
	{
	  if (bVerbose)
	    fputs(", Setting sparse attribute", stderr);

	  DWORD dwDevIOBytes;
	  if (!DeviceIoControl(hStream, FSCTL_SET_SPARSE,
			       NULL, 0, NULL, 0,
			       &dwDevIOBytes, NULL))
	    {
	      if (bVerbose)
		{
		  WErrMsgA errmsg;
		  oem_printf(stderr, ", Failed: %1", errmsg);
		}
	    }
	  else if (bVerbose)
	    fputs(", Ok", stderr);
	}

      if ((header.Size.QuadPart > 0) &
	  (header.dwStreamAttributes == STREAM_NORMAL_ATTRIBUTE))
	{
	  DWORD offset = HEADER_SIZE + header.dwStreamNameSize;
	  LPBYTE data = Buffer + offset;
	  DWORD size = dwBytesRead - offset;

	  for (;;)
	    {
	      DWORD dwIO;
	      BOOL bResult =
		WriteFile(hStream, data, size, &dwIO, NULL);

	      if ((!bResult) | (dwIO != size))
		{
		  WErrMsgA errmsg;
		  oem_printf(stderr,
			     "Error writing stream data '%2!.*ws!': %1%%n",
			     errmsg,
			     complete_name.Length >> 1,
			     complete_name.Buffer);

		  NativeDeleteFile(hStream);
		  SkipArchive(BytesToRead);
		  break;
		}

	      if (BytesToRead->QuadPart == 0)
		break;

	      data = Buffer;
	      if (BytesToRead->QuadPart > dwBufferSize)
		size = dwBufferSize;
	      else
		size = BytesToRead->LowPart;

	      dwBytesRead = ReadArchive(data, size);

	      if (dwBytesRead != size)
		{
		  fprintf(stderr,
			  "strarc: Incomplete stream: %u bytes missing.\n",
			  size - dwBytesRead);
		  status_exit(XE_ARCHIVE_TRUNC);
		}

	      BytesToRead->QuadPart -= dwBytesRead;
	    }
	}
      else
	SkipArchive(BytesToRead);

      for (;;)
	{
	  dwBytesRead = ReadArchive(Buffer, HEADER_SIZE);

	  if (dwBytesRead == 0)
	    {
	      NtClose(hStream);
	      return false;
	    }

	  if (dwBytesRead != HEADER_SIZE)
	    {
	      fprintf
		(stderr,
		 "strarc: Incomplete stream header: %u bytes missing.\n",
		 HEADER_SIZE - dwBytesRead);
	      status_exit(XE_ARCHIVE_TRUNC);
	    }

	  if (IsNewFileHeader())
	    {
	      NtClose(hStream);
	      return true;
	    }

	  FillEntireBuffer(dwBytesRead, BytesToRead);

	  // Another named alternate stream follows. Switch to restoring
	  // that one.
	  if (header.dwStreamNameSize > 0)
	    break;

	  bool bResult =
	    WriteFileFromArchive(hStream,
				 dwBytesRead,
				 BytesToRead,
				 complete_name,
				 bSeekOnly);

	  if (!bResult)
	    {
	      NativeDeleteFile(hStream);
	      NtClose(hStream);
	      return false;
	    }
	}

      NtClose(hStream);
    }
}

/* wczFile      - Name of file to restore with relative path.
 * FileInfo     - Structure with file attributes and file times to restore.
 * wczShortName - The 8.3 name to restore (max 13 characters, without path).
 */
bool
RestoreFile(LPCWSTR wczFile,
	    const BY_HANDLE_FILE_INFORMATION * FileInfo,
	    LPCWSTR wczShortName)
{
  bool bSeekOnly = false;

  if (wczShortName != NULL)
    if (wczShortName[0] == 0)
      wczShortName = NULL;

  // Special handling of the -8 command line switch. Copy the short name buffer
  // to the filename part of the full path buffer and set the short name buffer
  // pointer to NULL so that later code does not attempt to set a short name.
  if ((bRestoreShortNamesOnly) && (wczShortName != NULL) &&
      !(FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    if (wcslen(wczFile) <=
	sizeof(wczFullPathBuffer) / sizeof(*wczFullPathBuffer))
      {
	wcscpy(wczFullPathBuffer, wczFile);

	for (LPWSTR wczPathSlash = wcschr(wczFullPathBuffer, L'/');
	     wczPathSlash != NULL;
	     wczPathSlash = wcschr(wczPathSlash + 1, L'/'))
	  *wczPathSlash = L'\\';

	LPWSTR wczNamePart = wcsrchr(wczFullPathBuffer, L'\\');
	if (wczNamePart == NULL)
	  wczNamePart = wczFullPathBuffer;
	else
	  wczNamePart++;

	if (wcslen(wczShortName) <
	    (sizeof(wczFullPathBuffer) / sizeof(*wczFullPathBuffer) -
	     (wczNamePart - wczFile)))
	  {
	    wcscpy(wczNamePart, wczShortName);
	    wczFile = wczFullPathBuffer;
	    wczShortName = NULL;
	  }
      }

  bool bSkipThis = ExcludedString(wczFile);

  if ((!bSkipThis) && bOverwriteArchived)
    {
      DWORD dwFileAttr = GetFileAttributes(wczFile);
      if (dwFileAttr != INVALID_FILE_ATTRIBUTES)
	if (dwFileAttr & FILE_ATTRIBUTE_ARCHIVE)
	  bSkipThis = true;
    }

  if ((!bSkipThis) && bOverwriteOlder)
    {
      WFilteredFileFinder found(wczFile, FILE_ATTRIBUTE_DIRECTORY);
      if (found)
	{
	  if (CompareFileTime(&found.ftLastWriteTime,
			      &FileInfo->ftLastWriteTime) >= 0)
	    bSkipThis = true;
	}
      else if (bFreshenExisting)
	bSkipThis = true;
    }

  if (bVerbose)
    {
      oem_printf(stderr, "%1%2!ws!", bSkipThis ? "Skipping: " : "", wczFile);
      if (wczShortName != NULL)
	oem_printf(stderr, ", %1!ws!", wczShortName);
    }
  else if (bTestMode | bListFiles)
    if (!bSkipThis)
      {
	char *szFile = WideToOemAlloc(wczFile);
	puts(szFile);
	hfree(szFile);
      }

  HANDLE hFile = INVALID_HANDLE_VALUE;

  LPCWSTR wczFullPath = wczFile;
  if (bTestMode | bSkipThis)
    bSeekOnly = true;
  else if (FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    if ((wczFile[0] == L'.') && (wczFile[1] == 0) && !bSeekOnly)
      {
	DWORD dw =
	  GetCurrentDirectory(sizeof(wczFullPathBuffer) /
			      sizeof(*wczFullPathBuffer),
			      wczFullPathBuffer);

	if ((dw == 0) | (dw + 1 > sizeof(wczFullPathBuffer) /
			 sizeof(*wczFullPathBuffer)))
	  {
	    if (bVerbose)
	      win_perrorA("Restoring current directory, but the current path "
			  "cannot be retrieved");
	  }
	else
	  {
	    wcscat(wczFullPathBuffer, L"\\");
	    wczFullPath = wczFullPathBuffer;

	    if (bVerbose)
	      oem_printf(stderr, ", full path: '%1!ws!'", wczFullPath);
	  }

	if (!bSeekOnly)
	  hFile = CreateFile(wczFullPath,
			     GENERIC_READ | GENERIC_WRITE | WRITE_DAC |
			     WRITE_OWNER | ACCESS_SYSTEM_SECURITY,
			     FILE_SHARE_READ | FILE_SHARE_WRITE |
			     FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			     FILE_ATTRIBUTE_NORMAL |
			     FILE_FLAG_BACKUP_SEMANTICS, NULL);
      }
    else
      {
	if (!bSeekOnly)
	  {
	    // DELETE permission is needed to set short filename. However, a
	    // directory cannot be opened with the DELETE permission if
	    // another process has set the directory or any subdirs as it's
	    // current directory, therefore we first try with DELETE and then
	    // without if the first try fails.
	    hFile = CreateFile(wczFile,
			       GENERIC_READ | GENERIC_WRITE | DELETE |
			       WRITE_DAC | WRITE_OWNER |
			       ACCESS_SYSTEM_SECURITY,
			       FILE_SHARE_READ | FILE_SHARE_WRITE |
			       FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			       FILE_ATTRIBUTE_NORMAL |
			       FILE_FLAG_BACKUP_SEMANTICS, NULL);

	    if ((hFile == INVALID_HANDLE_VALUE) ?
		(GetLastError() == ERROR_ACCESS_DENIED) : false)
	      hFile = CreateFile(wczFile,
				 GENERIC_READ | GENERIC_WRITE | WRITE_DAC |
				 WRITE_OWNER | ACCESS_SYSTEM_SECURITY,
				 FILE_SHARE_READ | FILE_SHARE_WRITE |
				 FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
				 FILE_ATTRIBUTE_NORMAL |
				 FILE_FLAG_BACKUP_SEMANTICS, NULL);
	  }

	if ((hFile == INVALID_HANDLE_VALUE) ?
	    (GetLastError() == ERROR_FILE_NOT_FOUND) : false)
	  {
	    if (!CreateDirectoryPath(wczFile))
	      if (win_errno != ERROR_ALREADY_EXISTS)
		bSeekOnly = true;

	    SetFileAttributes(wczFile, GetFileAttributes(wczFile) &
			      ~FILE_ATTRIBUTE_READONLY);

	    if (!bSeekOnly)
	      // Always request DELETE permissions on directories created by
	      // this restore operation.
	      hFile = CreateFile(wczFile,
				 GENERIC_READ | GENERIC_WRITE | DELETE |
				 WRITE_DAC | WRITE_OWNER |
				 ACCESS_SYSTEM_SECURITY,
				 FILE_SHARE_READ | FILE_SHARE_WRITE |
				 FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
				 FILE_ATTRIBUTE_NORMAL |
				 FILE_FLAG_BACKUP_SEMANTICS, NULL);
	  }
      }
  else if (!bSeekOnly)
    {
      SetFileAttributes(wczFile, GetFileAttributes(wczFile) &
			~FILE_ATTRIBUTE_READONLY);

      hFile =
	CreateFile(wczFile,
		   GENERIC_READ | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER
		   | DELETE | ACCESS_SYSTEM_SECURITY,
		   FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
		   dwExtractCreation,
		   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);

      if ((hFile == INVALID_HANDLE_VALUE) ?
	  (GetLastError() == ERROR_PATH_NOT_FOUND) &&
	  (wcslen(wczFile) <
	   sizeof(wczFullPathBuffer) / sizeof(*wczFullPathBuffer)) :
	  false)
	{
	  wcscpy(wczFullPathBuffer, wczFile);
	  for (LPWSTR wczPathSlash = wcschr(wczFullPathBuffer, L'/');
	       wczPathSlash != NULL;
	       wczPathSlash = wcschr(wczPathSlash + 1, L'/'))
	    *wczPathSlash = L'\\';

	  LPWSTR wczPathSep = wcsrchr(wczFullPathBuffer, L'\\');
	  if (wczPathSep != NULL)
	    {
	      *wczPathSep = 0;
	      if (CreateDirectoryPath(wczFullPathBuffer))
		hFile = CreateFile(wczFile, GENERIC_READ | GENERIC_WRITE |
				   WRITE_DAC | WRITE_OWNER | DELETE |
				   ACCESS_SYSTEM_SECURITY,
				   FILE_SHARE_READ | FILE_SHARE_DELETE,
				   NULL, dwExtractCreation,
				   FILE_ATTRIBUTE_NORMAL |
				   FILE_FLAG_BACKUP_SEMANTICS,
				   NULL);
	      else
		{
		  WErrMsgA errmsg;
		  oem_printf
		    (stderr,
		     "strarc: Error creating directory '%1!ws!': %2%%n",
		     wczFullPathBuffer, errmsg);
		}
	    }
	}
    }

  if ((!bTestMode) && (!bSkipThis) && (hFile == INVALID_HANDLE_VALUE))
    {
      WErrMsgA errmsg;
      oem_printf(stderr, "strarc: Cannot create '%1!ws!': %2%%n", wczFullPath,
		 errmsg);
      bSeekOnly = true;
    }

  if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) &&
      (hFile != INVALID_HANDLE_VALUE) && (!bSeekOnly))
    {
      if (bRestoreCompression)
	{
	  if (bVerbose)
	    fputs(", Setting sparse attribute", stderr);

	  DWORD dwDevIOBytes;
	  if (!DeviceIoControl(hFile, FSCTL_SET_SPARSE,
			       NULL, 0, NULL, 0,
			       &dwDevIOBytes, NULL))
	    {
	      if (bVerbose)
		{
		  WErrMsgA errmsg;
		  oem_printf(stderr, ", Failed: %1", errmsg);
		}
	    }
	  else if (bVerbose)
	    fputs(", Ok", stderr);
	}
    }

  if ((wczShortName != NULL) && (hFile != INVALID_HANDLE_VALUE) &&
      (!bSeekOnly))
    if (!SetShortFileName(hFile, wczShortName))
      if (bVerbose)
	{
	  WErrMsgA errmsg;
	  oem_printf(stderr,
		     "%%n"
		     "strarc: Cannot set short name on '%1!ws!': %2%%n",
		     wczFullPath, errmsg);
	}
      else
	switch (win_errno)
	  {
	  case ERROR_INVALID_PARAMETER:
	  case ERROR_BAD_LENGTH:
	    break;
	  default:
	    {
	      WErrMsgA errmsg;
	      oem_printf(stderr,
			 "strarc: Cannot set short name on '%1!ws!': %2%%n",
			 wczFullPath, errmsg);
	    }
	  }

  DWORD dwBytesRead;

  for (;;)
    {
      YieldSingleProcessor();

      if (bCancel)
	{
	  if (hFile != INVALID_HANDLE_VALUE)
	    CloseHandle(hFile);
	  DeleteFile(wczFullPath);
	  return false;
	}

      dwBytesRead = ReadArchive(Buffer, HEADER_SIZE);

      if (dwBytesRead == 0)
	break;

      if (dwBytesRead != HEADER_SIZE)
	{
	  fprintf(stderr,
		  "strarc: Incomplete stream header: %u bytes missing.\n",
		  HEADER_SIZE - dwBytesRead);
	  status_exit(XE_ARCHIVE_TRUNC);
	}

      if (IsNewFileHeader())
	break;

      ULARGE_INTEGER BytesToRead;

      FillEntireBuffer(dwBytesRead, &BytesToRead);

      if ((header.dwStreamId == BACKUP_LINK) && (!bSeekOnly))
	{
	  if (!WriteHardLinkFromArchive(hFile,
					dwBytesRead,
					wczFullPath,
					wczFile,
					bSeekOnly))
	    return false;
	}
      else if ((!bSeekOnly) &
	       (header.dwStreamId == BACKUP_ALTERNATE_DATA))
	{
	  UNICODE_STRING converted_name;
	  RtlDosPathNameToNtPathName_U(wczFile, &converted_name, NULL, NULL);

	  UNICODE_STRING file_base_name;
	  file_base_name.Length = 0;
	  file_base_name.MaximumLength = (USHORT)
	    (converted_name.Length + header.dwStreamNameSize + MAX_PATH);
	  file_base_name.Buffer = (PWSTR)
	    halloc_seh(file_base_name.MaximumLength);
	  RtlCopyUnicodeString(&file_base_name, &converted_name);
	  RtlFreeUnicodeString(&converted_name);

	  if (!RestoreAlternateStream(dwBytesRead,
				      &BytesToRead,
				      FILE_ATTRIBUTE_NORMAL,
				      &file_base_name,
				      bSeekOnly))
	    {
	      hfree(file_base_name.Buffer);
	      return false;
	    }

	  hfree(file_base_name.Buffer);
	  break;
	}
      else
	{
	  UNICODE_STRING FullPath;
	  RtlInitUnicodeString(&FullPath, wczFullPath);

	  if (!WriteFileFromArchive(hFile,
				    dwBytesRead,
				    &BytesToRead,
				    FullPath,
				    bSeekOnly))
	    return false;
	}
    }

  if ((!bSeekOnly) & (hFile != INVALID_HANDLE_VALUE))
    {
      BY_HANDLE_FILE_INFORMATION FileInfoNow;
      if (GetFileInformationByHandle(hFile, &FileInfoNow))
	{
	  if (bRestoreCompression &&
	      ((FileInfoNow.dwFileAttributes ^ FileInfo->dwFileAttributes) &
	       FILE_ATTRIBUTE_COMPRESSED))
	    {
	      USHORT usCommand;

	      if (FileInfo->dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED)
		usCommand = COMPRESSION_FORMAT_DEFAULT;
	      else
		usCommand = COMPRESSION_FORMAT_NONE;

	      if (bVerbose)
		oem_printf(stderr,
			   "strarc: %1ompressing '%2!ws!'...",
			   usCommand ==
			   COMPRESSION_FORMAT_NONE ? "Dec" : "C",
			   wczFullPath);

	      DWORD dwDevIOBytes;
	      if (!DeviceIoControl(hFile, FSCTL_SET_COMPRESSION,
				   &usCommand, sizeof usCommand, NULL, 0,
				   &dwDevIOBytes, NULL))
		{
		  if (bVerbose)
		    {
		      WErrMsgA errmsg;
		      oem_printf(stderr, " Failed: %1", errmsg);
		    }
		}
	      else if (bVerbose)
		fputs(" Ok.\r\n", stderr);
	    }

	  // This seems not to be necessary. Calling SetEndOfFile() before each
	  // sparse block restore operation should be enough to mark correct
	  // EOF.

// 	  if ((FileInfoNow.nFileSizeHigh != FileInfo->nFileSizeHigh) |
// 		  (FileInfoNow.nFileSizeLow != FileInfo->nFileSizeLow))
// 	    {
// 	      if (bVerbose)
// 		fputs(", Adjusting file size", stderr);

// 	      LONG dwDevIOBytes = (LONG) FileInfo->nFileSizeHigh;
// 	      if ((SetFilePointer(hFile, FileInfo->nFileSizeLow,
// 				  &dwDevIOBytes, FILE_BEGIN) ==
// 		   INVALID_SET_FILE_POINTER) ?
// 		  (GetLastError() != NO_ERROR) :
// 		  (!SetEndOfFile(hFile) ?
// 		   (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) ==
// 		    INVALID_SET_FILE_POINTER) : false))
// 		{
// 		  if (bVerbose)
// 		    {
// 		      WErrMsgA errmsg;
// 		      oem_printf(stderr, ", Failed: %1", errmsg);
// 		    }
// 		}
// 	      else if (bVerbose)
// 		fputs(", Ok", stderr);
// 	    }
	}
      else if (bVerbose)
	{
	  WErrMsgA errmsg;
	  oem_printf(stderr, "Cannot get file attributes: %1", errmsg);
	}

      if (bProcessFileTimes)
	if (!SetFileTime(hFile,
			 &FileInfo->ftCreationTime,
			 &FileInfo->ftLastAccessTime,
			 &FileInfo->ftLastWriteTime))
	  {
	    WErrMsgA errmsg;
	    oem_printf(stderr,
		       "strarc: Unable to set time stamps on '%1!ws!': %2%%n",
		       wczFullPath, errmsg);
	  }
    }

  if (bVerbose)
    fputs("\r\n", stderr);

  if (((hFile != INVALID_HANDLE_VALUE) | bTestMode) && (!bSkipThis))
    ++dwFileCounter;

  if (hFile != INVALID_HANDLE_VALUE)
    CloseHandle(hFile);

  if ((!bSeekOnly) && (bProcessFileAttribs))
    if (!SetFileAttributes(wczFullPath, FileInfo->dwFileAttributes))
      {
	WErrMsgA errmsg;
	oem_printf(stderr,
		   "strarc: Unable to restore attributes for '%1!ws!': %2%%n",
		   wczFullPath, errmsg);
      }

  return dwBytesRead > 0;
}

void
RestoreDirectoryTree()
{
  if (dwBufferSize < HEADER_SIZE)
    status_exit(XE_BAD_BUFFER);

  if (!ReadArchiveHeader(&header))
    return;

  for (;;)
    {
      YieldSingleProcessor();

      if ((header.dwStreamId != BACKUP_INVALID) |
	  (header.dwStreamAttributes != STRARC_MAGIC) |
	  ((header.Size.QuadPart != sizeof BY_HANDLE_FILE_INFORMATION) &&
	   (header.Size.QuadPart != sizeof BY_HANDLE_FILE_INFORMATION + 26)) |
	  (header.dwStreamNameSize == 0) |
	  (header.dwStreamNameSize > 65535) | (header.dwStreamNameSize & 1))
	if (!ReadArchiveHeader(&header))
	  return;

      DWORD dwBytesToRead = header.dwStreamNameSize + header.Size.LowPart;
      if (dwBufferSize - HEADER_SIZE < dwBytesToRead)
	status_exit(XE_BAD_BUFFER);
      DWORD dwBytesRead = ReadArchive(Buffer + HEADER_SIZE, dwBytesToRead);

      if (dwBytesRead != dwBytesToRead)
	{
	  if (!ReadArchiveHeader(&header))
	    status_exit(XE_ARCHIVE_TRUNC);

	  continue;
	}

      LPWSTR wczFileName = (LPWSTR) halloc(header.dwStreamNameSize + 2);
      if (wczFileName == NULL)
	status_exit(XE_NOT_ENOUGH_MEMORY);
      wcsncpy(wczFileName, header.cStreamName, header.dwStreamNameSize >> 1);
      wczFileName[header.dwStreamNameSize >> 1] = 0;

      BY_HANDLE_FILE_INFORMATION FileInfo;
      CopyMemory(&FileInfo, Buffer + HEADER_SIZE + header.dwStreamNameSize,
		 sizeof FileInfo);

      WCHAR wczShortName[14] = L"";
      if (header.Size.QuadPart == sizeof(BY_HANDLE_FILE_INFORMATION) + 26)
	{
	  CopyMemory(wczShortName, Buffer + HEADER_SIZE +
		     header.dwStreamNameSize +
		     sizeof BY_HANDLE_FILE_INFORMATION, 26);
	  wczShortName[13] = 0;
	}

      if (!RestoreFile(wczFileName, &FileInfo, wczShortName))
	{
	  hfree(wczFileName);
	  return;
	}

      hfree(wczFileName);
    }
}
