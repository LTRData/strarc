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

#include <winstrct.h>
#include <winioctl.h>
#include <wfind.h>

#include "strarc.hpp"
#include "lnk.h"

#pragma comment(lib, "crthlp.lib")
#pragma comment(lib, "crtdll.lib")

/* wczFile - Name of file to restore with relative path.
 * FileInfo - Structure with file attributes and file times to restore.
 * wczShortName - The 8.3 name to restore (max 13 characters, without path).
 */
bool
RestoreFile(LPCWSTR wczFile, const BY_HANDLE_FILE_INFORMATION * FileInfo,
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
			     FILE_FLAG_BACKUP_SEMANTICS |
			     FILE_FLAG_SEQUENTIAL_SCAN, NULL);
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
			       FILE_FLAG_BACKUP_SEMANTICS |
			       FILE_FLAG_SEQUENTIAL_SCAN, NULL);

	    if ((hFile == INVALID_HANDLE_VALUE) ?
		(GetLastError() == ERROR_ACCESS_DENIED) : false)
	      hFile = CreateFile(wczFile,
				 GENERIC_READ | GENERIC_WRITE | WRITE_DAC |
				 WRITE_OWNER | ACCESS_SYSTEM_SECURITY,
				 FILE_SHARE_READ | FILE_SHARE_WRITE |
				 FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
				 FILE_ATTRIBUTE_NORMAL |
				 FILE_FLAG_BACKUP_SEMANTICS |
				 FILE_FLAG_SEQUENTIAL_SCAN, NULL);
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
				 FILE_FLAG_BACKUP_SEMANTICS |
				 FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	  }
      }
  else
    {
      if (!bSeekOnly)
	{
	  SetFileAttributes(wczFile, GetFileAttributes(wczFile) &
			    ~FILE_ATTRIBUTE_READONLY);

	  hFile =
	    CreateFile(wczFile,
		       GENERIC_READ | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER
		       | DELETE | ACCESS_SYSTEM_SECURITY,
		       FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
		       dwExtractCreation,
		       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS |
		       FILE_FLAG_SEQUENTIAL_SCAN, NULL);

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
				       FILE_FLAG_BACKUP_SEMANTICS |
				       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
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
    }

  if ((!bTestMode) && (!bSkipThis) && (hFile == INVALID_HANDLE_VALUE))
    {
      WErrMsgA errmsg;
      oem_printf(stderr, "strarc: Cannot create '%1!ws!': %2%%n", wczFullPath,
		 errmsg);
      bSeekOnly = true;
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
      Sleep(0);

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

      if ((header.dwStreamId == BACKUP_INVALID) &&
	  (header.dwStreamAttributes == STRARC_MAGIC) &&
	  ((header.Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION) |
	   (header.Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION + 26))
	  && (header.dwStreamNameSize > 0)
	  && (header.dwStreamNameSize <= 65535))
	break;

      if (header.dwStreamNameSize & 1)
	break;

      if (bVerbose)
	fprintf(stderr, ", [id=%p, attr=%p, %u bytes, %p%p bytes]",
		header.dwStreamId, header.dwStreamAttributes,
		header.dwStreamNameSize,
		header.Size.HighPart, header.Size.LowPart);

      ULARGE_INTEGER BytesToRead = { 0 };
      LPVOID lpCtx = NULL;

      if ((header.dwStreamId == BACKUP_LINK) && (!bSeekOnly))
	{
	  CloseHandle(hFile);
	  hFile = INVALID_HANDLE_VALUE;
	  DeleteFile(wczFullPath);

	  WCHAR wczLinkName[32768];

	  // If relative name is longer than 32767 wchars it won't fit in
	  // the temp buffer here. Consider that as an archive format error.
	  if ((header.Size.QuadPart >= sizeof wczLinkName) |
	      (header.dwStreamNameSize > 0))
	    status_exit(XE_ARCHIVE_BAD_HEADER);

	  // Read the stream with the relative name into a temp buffer.
	  dwBytesRead =
	    ReadArchive((LPBYTE) wczLinkName, header.Size.LowPart);

	  if (dwBytesRead != header.Size.QuadPart)
	    {
	      if (bVerbose)
		oem_printf(stderr,
			   " incomplete stream: Possibly damaged header or "
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
	      wczLinkName[dwBytesRead >> 1] = 0;

	      if (bVerbose)
		oem_printf(stderr, ", link '%1!ws!'", wczLinkName);
	    }

	  if (bHardLinkSupport & !bSeekOnly)
	    {
	      hFile = CreateFile(wczLinkName, 0,
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
			     wczLinkName, errmsg, wczFile);
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
				 wczFile, errmsg, wczLinkName);
		    }

		if (hFile != INVALID_HANDLE_VALUE)
		  {
		    CloseHandle(hFile);
		    hFile = INVALID_HANDLE_VALUE;
		  }
		if (!CopyFile(wczLinkName, wczFile, FALSE))
		  {
		    WErrMsgA errmsg;
		    oem_printf(stderr,
			       "strarc: %1%%n"
			       "Failed both linking and copying '%2!ws!' to "
			       "'%3!ws!'.%%n",
			       errmsg, wczLinkName, wczFile);
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
				       FILE_FLAG_BACKUP_SEMANTICS |
				       FILE_FLAG_SEQUENTIAL_SCAN, NULL);
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
	}
      else
	{
	  BytesToRead.QuadPart =
	    header.dwStreamNameSize + header.Size.QuadPart;

	  if (!bSeekOnly)
	    if (!BackupWrite(hFile, Buffer, HEADER_SIZE,
			     &dwBytesRead, FALSE, bProcessSecurity, &lpCtx))
	      {
		WErrMsgA errmsg;
		oem_printf(stderr, "strarc: Cannot write '%1!ws!': %2%%n",
			   wczFullPath, errmsg);
		bSeekOnly = true;
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
		DeleteFile(wczFullPath);
	      }
	}

      while (BytesToRead.QuadPart > 0)
	{
	  Sleep(0);

	  if (bCancel)
	    {
	      if (hFile != INVALID_HANDLE_VALUE)
		CloseHandle(hFile);
	      DeleteFile(wczFullPath);
	      return false;
	    }

	  dwBytesRead = ReadArchive(Buffer,
				    BytesToRead.QuadPart >
				    (DWORDLONG) dwBufferSize ?
				    dwBufferSize : BytesToRead.LowPart);

	  BytesToRead.QuadPart -= dwBytesRead;

	  if (dwBytesRead == 0)
	    {
	      if (bVerbose)
		fprintf(stderr, ", %.4g %s missing.\r\n",
			_h(BytesToRead.QuadPart), _p(BytesToRead.QuadPart));
	      status_exit(XE_ARCHIVE_TRUNC);
	    }

	  if (!bSeekOnly)
	    if (!BackupWrite(hFile, Buffer, dwBytesRead, &dwBytesRead, FALSE,
			     bProcessSecurity, &lpCtx))
	      {
		WErrMsgA errmsg;
		oem_printf(stderr,
			   "strarc: Restore of '%1!ws!' incomplete: %2%%n",
			   wczFullPath, errmsg);

		if (lpCtx != NULL)
		  BackupWrite(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);

		if (hFile != INVALID_HANDLE_VALUE)
		  CloseHandle(hFile);

		DeleteFile(wczFullPath);

		return false;
	      }
	}

      if (lpCtx != NULL)
	BackupWrite(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);
    }

  if (bVerbose)
    fputs("\r\n", stderr);

  if ((!bSeekOnly) & (hFile != INVALID_HANDLE_VALUE))
    {
      if (bProcessFileTimes)
	if (!SetFileTime(hFile, &FileInfo->ftCreationTime,
			 &FileInfo->ftLastAccessTime,
			 &FileInfo->ftLastWriteTime))
	  {
	    WErrMsgA errmsg;
	    oem_printf(stderr,
		       "strarc: Unable to set time stamps on '%1!ws!': %2%%n",
		       wczFullPath, errmsg);
	  }

      if (bRestoreCompression)
	{
	  BY_HANDLE_FILE_INFORMATION FileInfoNow;
	  if (GetFileInformationByHandle(hFile, &FileInfoNow))
	    {
	      if (FileInfoNow.dwFileAttributes ^ FileInfo->dwFileAttributes &
		  FILE_ATTRIBUTE_COMPRESSED)
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

	      if ((~FileInfoNow.dwFileAttributes) &
		  FileInfoNow.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE)
		{
		  if (bVerbose)
		    oem_printf(stderr,
			       "strarc: Setting sparse attribute '%1!ws!'...",
			       wczFullPath);

		  DWORD dwDevIOBytes;
		  if (!DeviceIoControl(hFile, FSCTL_SET_SPARSE,
				       NULL, 0, NULL, 0,
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
	    }
	  else if (bVerbose)
	    {
	      WErrMsgA errmsg;
	      oem_printf(stderr, "Cannot get file attributes: %1", errmsg);
	    }
	}
    }

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
      Sleep(0);

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
