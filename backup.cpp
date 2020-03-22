/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2013
 *
 * backup.cpp
 * Backup operation source file.
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

#include <winstrct.h>

#include "sleep.h"

#include <wfind.h>

#include "strarc.hpp"
#include "linktrack.hpp"

#ifndef _WIN64
#pragma comment(lib, "crthlp.lib")
#pragma comment(lib, "crtdll.lib")
#endif

// This function backs up an object (file or directory) to the archive stream.

// wczFile is the complete relative path from current directory to the object
// to backup. wczShortName is the alternate short 8.3 name to store in the
// header. If no short name should be stored for this file, set this parameter
// to an empty string, L"". If the short name should be stored but is not
// known when calling this function, set this parameter to NULL.
bool
BackupFile(LPWSTR wczFile, LPCWSTR wczShortName)
{
  if (ExcludedString(wczFile))
    return true;

  if ((BackupMethod == BACKUP_METHOD_DIFF) |
      (BackupMethod == BACKUP_METHOD_INC))
    {
      if (!(GetFileAttributes(wczFile) & FILE_ATTRIBUTE_ARCHIVE))
	return true;
    }

  if (bListFiles)
    {
      char *szFile = WideToOemAlloc(wczFile);
      puts(szFile);
      hfree(szFile);
    }

  if (wczShortName == NULL)
    {
      WFileFinder finddata(wczFile);
      if (finddata)
	{
	  wczShortName = finddata.cAlternateFileName;
	  if (wczShortName[0] == 0)
	    wczShortName = NULL;
	}
    }
  else if (wczShortName[0] == 0)
    wczShortName = NULL;

  if (bVerbose)
    {
      oem_printf(stderr, "%1!ws!", wczFile);
      if (wczShortName != NULL)
	oem_printf(stderr, ", short: %1!ws!", wczShortName);
    }

  DWORD dwCreateFileFlags =
    FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS;

  // Check if this is a reparse point. In that case, do not follow it if the
  // -j switch is given at the command line.
  if (bLocal)
    {
      DWORD dwFileAttr = GetFileAttributes(wczFile);
      if (dwFileAttr == INVALID_FILE_ATTRIBUTES)
	dwFileAttr = 0;

      if (dwFileAttr & FILE_ATTRIBUTE_REPARSE_POINT)
	{
	  if (bVerbose)
	    fputs(", reparse point", stderr);
	  dwCreateFileFlags |= FILE_FLAG_OPEN_REPARSE_POINT;
	}
    }

  HANDLE hFile = INVALID_HANDLE_VALUE;
  LPCWSTR wczFullPath = wczFile;
  if ((wczFile[0] == L'.') & (wczFile[1] == 0))
    {
      DWORD dw = GetCurrentDirectory(sizeof(wczFullPathBuffer) /
				     sizeof(*wczFullPathBuffer),
				     wczFullPathBuffer);

      if ((dw == 0) | (dw + 1 > sizeof(wczFullPathBuffer) /
		       sizeof(*wczFullPathBuffer)))
	{
	  if (bVerbose)
	    win_perrorA("Backing up current directory, but the current path "
			"cannot be retrieved. Metadata for the current "
			"directory will not be backed up.%%n"
			"Error");
	}
      else
	{
	  wcscat(wczFullPathBuffer, L"\\");
	  wczFullPath = wczFullPathBuffer;

	  if (bVerbose)
	    oem_printf(stderr,
		       "%%n"
		       "Backing up current directory using full path: "
		       "'%1!ws!'%%n",
		       wczFullPath);
	}
    }

  hFile = CreateFile(wczFullPath, GENERIC_READ, FILE_SHARE_READ |
		     FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		     OPEN_EXISTING, dwCreateFileFlags, NULL);

  if (hFile == INVALID_HANDLE_VALUE)
    {
      WErrMsgA errmsg;
      oem_printf(stderr, "strarc: Cannot open '%1!ws!': %2%%n", wczFullPath,
		 errmsg);
      return false;
    }

  // Check if this is a registry snapshot file.
  if (bBackupRegistrySnapshots)
    if (wcslen(wczFile) >=
	(sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION) /
	 sizeof(*REGISTRY_SNAPSHOT_FILE_EXTENSION)))
      if (wcscmp(wczFile + wcslen(wczFile) -
		 (sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION) /
		  sizeof(*REGISTRY_SNAPSHOT_FILE_EXTENSION) - 1),
		 REGISTRY_SNAPSHOT_FILE_EXTENSION) == 0)
	{
	  DeleteFile(wczFile);

	  wczFile[wcslen(wczFile) -
		 (sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION) /
		  sizeof(*REGISTRY_SNAPSHOT_FILE_EXTENSION) - 1)] = 0;
	}

  header.dwStreamId = BACKUP_INVALID;
  header.dwStreamAttributes = STRARC_MAGIC;
  header.Size.QuadPart = sizeof BY_HANDLE_FILE_INFORMATION;
  if (wczShortName != NULL)
    header.Size.QuadPart += 26;
  header.dwStreamNameSize = (DWORD) wcslen(wczFile) << 1;
  wcsncpy(header.cStreamName, wczFile, header.dwStreamNameSize >> 1);
  BY_HANDLE_FILE_INFORMATION *FileInfo = (BY_HANDLE_FILE_INFORMATION *)
    (Buffer + HEADER_SIZE + header.dwStreamNameSize);
  if (!GetFileInformationByHandle(hFile, FileInfo))
    {
      WErrMsgA errmsg;
      oem_printf(stderr,
		 "strarc: Cannot get file information for '%1!ws!': %2%%n",
		 wczFile, errmsg);
      CloseHandle(hFile);
      return false;
    }

  if (BackupMethod != BACKUP_METHOD_COPY)
    FileInfo->dwFileAttributes &= ~FILE_ATTRIBUTE_ARCHIVE;

  if (wczShortName != NULL)
    CopyMemory(Buffer + HEADER_SIZE + header.dwStreamNameSize +
	       sizeof BY_HANDLE_FILE_INFORMATION, wczShortName, 26);

  if (bVerbose)
    fprintf(stderr, ", header: %u bytes",
	    HEADER_SIZE + header.dwStreamNameSize + header.Size.LowPart);

  WriteArchive(Buffer, HEADER_SIZE + header.dwStreamNameSize +
	       header.Size.LowPart);

  LPCWSTR wczLinkName = NULL;
  if ((FileInfo->nNumberOfLinks > 1) && (bHardLinkSupport))
    {
      LARGE_INTEGER FileIndex;
      FileIndex.LowPart = FileInfo->nFileIndexLow;
      FileIndex.HighPart = FileInfo->nFileIndexHigh;

      wczLinkName = MatchLink(FileInfo->dwVolumeSerialNumber,
			      FileIndex.QuadPart, wczFile);
    }

  if (wczLinkName != NULL)
    {
      CloseHandle(hFile);

      header.dwStreamId = BACKUP_LINK;
      header.dwStreamAttributes = 0;
      header.Size.QuadPart = wcslen(wczLinkName) << 1;
      header.dwStreamNameSize = 0;
      WriteArchive(Buffer, HEADER_SIZE);

      if (bVerbose)
	oem_printf(stderr, ", link to '%1!ws!': %2!u! bytes%%n",
		   wczLinkName, HEADER_SIZE + header.Size.LowPart);

      LPBYTE writebuffer = (LPBYTE) wczLinkName;
      while (header.Size.QuadPart > 0)
	{
	  DWORD dwWriteLength = (header.Size.QuadPart > dwBufferSize) ?
	    dwBufferSize : header.Size.LowPart;
	  WriteArchive(writebuffer, dwWriteLength);
	  writebuffer += dwWriteLength;
	  header.Size.QuadPart -= dwWriteLength;
	}

      ++dwFileCounter;
      return true;
    }

  LPVOID lpCtx = NULL;
  for (;;)
    {
      YieldSingleProcessor();

      if (bCancel)
	{
	  if (bVerbose)
	    fputs(", break.\r\n", stderr);

	  return false;
	}

      if (bVerbose)
	fputs(", stream: ", stderr);

      DWORD dwBytesRead;
      if (!BackupRead(hFile, Buffer, dwBufferSize, &dwBytesRead, FALSE,
		      bProcessSecurity, &lpCtx))
	{
	  WErrMsgA errmsg;
	  oem_printf(stderr, "strarc: Cannot read '%1!ws!': %2%%n",
		     wczFile, errmsg);
	  CloseHandle(hFile);
	  return false;
	}

      if (dwBytesRead == 0)
	{
	  BackupRead(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);
	  CloseHandle(hFile);
	  ++dwFileCounter;

	  if (bVerbose)
	    fputs("EOF\r\n", stderr);

	  if ((BackupMethod == BACKUP_METHOD_FULL) |
	      (BackupMethod == BACKUP_METHOD_INC))
	    {
	      DWORD dwFileAttr = GetFileAttributes(wczFullPath);
	      if (dwFileAttr != INVALID_FILE_ATTRIBUTES)
		SetFileAttributes(wczFullPath,
				  dwFileAttr & ~FILE_ATTRIBUTE_ARCHIVE);
	    }

	  return true;
	}

      if (bVerbose)
	fprintf(stderr, "%u bytes", dwBytesRead);

      WriteArchive(Buffer, dwBytesRead);
    }
}

void
BackupDirectory(LPWSTR wczPath)
{
  wcscpy(wczPath, L"*");
  WFileFinder finddata(wczCurrentPath);

  if (!finddata)
    return;

  do
    {
      YieldSingleProcessor();

      if (bCancel)
	return;

      if ((wcscmp(finddata.cFileName, L"..") == 0) |
	  (wcscmp(finddata.cFileName, L".") == 0))
	continue;

      if (wcslen(finddata.cFileName) >=
	  sizeof(wczCurrentPath) * sizeof(WCHAR) -
	  (wczPath - wczCurrentPath) - sizeof(WCHAR) * 2)
	{
	  oem_printf(stderr,
		     "%1!ws!\\%2!ws!: Path is too long.%%n",
		     wczCurrentPath, finddata.cFileName);

	  status_exit(XE_TOO_LONG_PATH);
	}

      wcscpy(wczPath, finddata.cFileName);

      if ((finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	  !(bLocal &&
	    (finddata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)))
	{
	  size_t iLen = wcslen(wczPath);

	  wcscat(wczPath, L"\\");

	  if (ExcludedString(wczCurrentPath))
	    {
	      if (bVerbose)
		oem_printf(stderr, "Skipping directory: '%1!ws!'%%n",
			   wczCurrentPath);
	    }
	  else
	    {
	      if (bVerbose)
		oem_printf(stderr, "Backing up directory: '%1!ws!'%%n",
			   wczCurrentPath);
	      BackupDirectory(wczPath + wcslen(wczPath));
	    }

	  wczPath[iLen] = 0;

	  if (!BackupFile(wczCurrentPath, finddata.cAlternateFileName))
	    {
	      if (bVerbose)
		oem_printf(stderr, "Skipping directory: '%1!ws!'%%n",
			   wczCurrentPath);

	      continue;
	    }
	}
      else
	BackupFile(wczCurrentPath, finddata.cAlternateFileName);
    }
  while (finddata.Next());
}

void
BackupDirectoryTree()
{
  size_t iLen = wcslen(wczCurrentPath);
  if (iLen == 0)
    return;

  size_t iLenOriginalEnd = iLen;

  if ((wczCurrentPath[iLen - 1] != L'\\') &
      (wczCurrentPath[iLen - 1] != L':') & (wczCurrentPath[iLen - 1] != L'/'))
    {
      wczCurrentPath[iLen++] = L'\\';
      wczCurrentPath[iLen] = 0;
    }

  BackupDirectory(wczCurrentPath + iLen);

  wczCurrentPath[iLenOriginalEnd] = 0;

  BackupFile(wczCurrentPath, NULL);

  if ((wczCurrentPath[iLen - 1] != L'\\') &
      (wczCurrentPath[iLen - 1] != L':') & (wczCurrentPath[iLen - 1] != L'/'))
    {
      wczCurrentPath[iLen++] = L'\\';
      wczCurrentPath[iLen] = 0;
    }
}
