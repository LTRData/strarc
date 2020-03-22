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

#include <windows.h>
#include <ntdll.h>
#include <intsafe.h>
#include <winstrct.h>

#include "strarc.hpp"

// This function backs up an object (file or directory) to the archive stream.

bool
StrArc::ReadFileStreamsToArchive(PUNICODE_STRING File,
				 HANDLE hFile)
{
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
	  oem_printf(stderr,
		     "strarc: Cannot read '%2!.*ws!': %1%%n",
		     errmsg,
		     File->Length >> 1, File->Buffer);
	  return false;
	}

      if (dwBytesRead == 0)
	{
	  BackupRead(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);
	  ++dwFileCounter;

	  if (bVerbose)
	    fputs("EOF\r\n", stderr);

	  return true;
	}

      if (bVerbose)
	fprintf(stderr, "%u bytes", dwBytesRead);

      WriteArchive(Buffer, dwBytesRead);
    }
}

// File is the complete relative path from current directory to the object
// to backup. ShortName is the alternate short 8.3 name to store in the backup
// stream header. If no short name should be stored for this file, set this
// parameter to an empty string, L"". If the short name should be stored but is
// not known when calling this function, set this parameter to NULL.
bool
StrArc::BackupFile(PUNICODE_STRING File,
		   PUNICODE_STRING ShortName,
		   bool bTraverseDirectories)
{
  // This could be a directory. In that case, we do not want to skip it just
  // because it does not match any of the -i strings, but still skip if it
  // matches any of the -e strings. This is to find files and directories in
  // this directory that may match an -i string.

  bool bExcludeThis;
  bool bIncludeThis;
  ExcludedString(File, &bExcludeThis, &bIncludeThis);
  if (bExcludeThis)
    return true;

  ACCESS_MASK file_access = GENERIC_READ;
  if ((BackupMethod == BACKUP_METHOD_FULL) |
      (BackupMethod == BACKUP_METHOD_INC))
    file_access |= FILE_WRITE_ATTRIBUTES;

  HANDLE hFile =
    NativeOpenFile(RootDirectory,
		   File,
		   file_access,
		   0,
		   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		   FILE_OPEN_FOR_BACKUP_INTENT |
		   FILE_SEQUENTIAL_ONLY |
		   (bLocal ? FILE_OPEN_REPARSE_POINT : 0));
  if ((!bLocal) & (hFile == INVALID_HANDLE_VALUE))
    if (GetLastError() == ERROR_INVALID_PARAMETER)
      hFile =
	NativeOpenFile(RootDirectory,
		       File,
		       file_access,
		       0,
		       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		       FILE_OPEN_FOR_BACKUP_INTENT |
		       FILE_SEQUENTIAL_ONLY);

  if (hFile == INVALID_HANDLE_VALUE)
    {
      WErrMsgA errmsg;
      oem_printf(stderr,
		 "strarc: Cannot open '%2!.*ws!': %1%%n",
		 errmsg,
		 File->Length >> 1, File->Buffer);
      return false;
    }

  BY_HANDLE_FILE_INFORMATION file_info;
  if (!GetFileInformationByHandle(hFile, &file_info))
    {
      WErrMsgA errmsg;
      oem_printf(stderr,
		 "strarc: Cannot get file information for '%2!.*ws!': %1%%n",
		 errmsg,
		 File->Length >> 1, File->Buffer);
      NtClose(hFile);
      return false;
    }

  if (bTraverseDirectories)
    if ((file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	!(bLocal &&
	  (file_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)))
      BackupDirectory(File, hFile);

  if (bCancel)
    {
      NtClose(hFile);
      return false;
    }

  if (!bIncludeThis)
    {
      NtClose(hFile);
      return true;
    }

  if (File->Length == 0)
    RtlInitUnicodeString(File, L".");

  if (bVerbose)
    {
      oem_printf(stderr,
		 "%1!.*ws!",
		 File->Length >> 1, File->Buffer);

      fprintf(stderr, ", attr=%#x", file_info.dwFileAttributes);
    }

  if ((BackupMethod == BACKUP_METHOD_DIFF) |
      (BackupMethod == BACKUP_METHOD_INC))
    if (file_info.dwFileAttributes & ~FILE_ATTRIBUTE_ARCHIVE)
      {
	NtClose(hFile);
	return true;
      }

  if (bListFiles)
    {
      OEM_STRING oem_file_name;
      NTSTATUS status =
	RtlUnicodeStringToOemString(&oem_file_name,
				    File,
				    TRUE);
      if (NT_SUCCESS(status))
	{
	  DWORD dwIO;
	  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		    oem_file_name.Buffer,
		    oem_file_name.Length,
		    &dwIO,
		    NULL);
	  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
		    "\r\n",
		    2,
		    &dwIO,
		    NULL);
	  RtlFreeOemString(&oem_file_name);
	}
    }

  if (bListOnly)
    {
      if (bVerbose)
	fputs("\r\n", stderr);

      ++dwFileCounter;
      NtClose(hFile);
      return true;
    }

  // Check if this is a registry snapshot file.
  if (bBackupRegistrySnapshots)
    if (File->Length >= sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION))
      if (wcscmp(File->Buffer + (File->Length -
				 sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION)) /
		 sizeof(*REGISTRY_SNAPSHOT_FILE_EXTENSION) + 1,
		 REGISTRY_SNAPSHOT_FILE_EXTENSION) == 0)
	{
	  NativeDeleteFile(hFile);

	  File->Length -= sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION) - 2;
	}

  // Save without archive attribute in archive, unless COPY mode.
  if (BackupMethod != BACKUP_METHOD_COPY)
    file_info.dwFileAttributes &= ~FILE_ATTRIBUTE_ARCHIVE;

  header->dwStreamId = BACKUP_INVALID;
  header->dwStreamAttributes = STRARC_MAGIC;
  header->Size.QuadPart = sizeof BY_HANDLE_FILE_INFORMATION;
  header->dwStreamNameSize = File->Length;
  memcpy(header->cStreamName, File->Buffer, header->dwStreamNameSize);
  *(PBY_HANDLE_FILE_INFORMATION)
    (Buffer + HEADER_SIZE + header->dwStreamNameSize) = file_info;

  if (ShortName == NULL)
    {
      UNICODE_STRING parent_dir;
      UNICODE_STRING file_part;
      SplitPath(File, &parent_dir, &file_part);

      HANDLE hParentDir =
	NativeOpenFile(RootDirectory,
		       &parent_dir,
		       FILE_LIST_DIRECTORY,
		       0,
		       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		       FILE_OPEN_FOR_BACKUP_INTENT);

      if (hParentDir != INVALID_HANDLE_VALUE)
	{
	  if (finddata.FindFirst(hParentDir, &file_part))
	    if (finddata.ShortNameLength > 0)
	      {
		header->Size.QuadPart += 26;

		LPSTR shortname = (LPSTR)
		  (Buffer + HEADER_SIZE + header->dwStreamNameSize +
		   sizeof BY_HANDLE_FILE_INFORMATION);

		memset(shortname, 0, 26);
		memcpy(shortname,
		       finddata.ShortName,
		       finddata.ShortNameLength);

		if (bVerbose)
		  oem_printf(stderr,
			     ", short='%1!.*ws!'",
			     finddata.ShortNameLength >> 1,
			     finddata.ShortName);
	      }

	  NtClose(hParentDir);
	}
    }
  else if ((ShortName != NULL) ? (ShortName->Length > 0) : false)
    {
      header->Size.QuadPart += 26;

      LPSTR shortname = (LPSTR)
	(Buffer + HEADER_SIZE + header->dwStreamNameSize +
	 sizeof BY_HANDLE_FILE_INFORMATION);

      memset(shortname, 0, 26);
      memcpy(shortname, ShortName->Buffer, ShortName->Length);

      if (bVerbose)
	oem_printf(stderr,
		   ", short='%1!.*ws!'",
		   ShortName->Length >> 1,
		   ShortName->Buffer);
    }

  if (bVerbose)
    fprintf(stderr, ", header: %u bytes",
	    HEADER_SIZE + header->dwStreamNameSize + header->Size.LowPart);

  WriteArchive(Buffer, HEADER_SIZE + header->dwStreamNameSize +
	       header->Size.LowPart);

  PUNICODE_STRING LinkName = NULL;
  if ((file_info.nNumberOfLinks > 1) && (bHardLinkSupport))
    {
      LARGE_INTEGER FileIndex;
      FileIndex.LowPart = file_info.nFileIndexLow;
      FileIndex.HighPart = file_info.nFileIndexHigh;

      LinkName =
	MatchLink(file_info.dwVolumeSerialNumber,
		  FileIndex.QuadPart,
		  File);
    }

  if (LinkName != NULL)
    {
      NtClose(hFile);

      header->dwStreamId = BACKUP_LINK;
      header->dwStreamAttributes = 0;
      header->Size.QuadPart = LinkName->Length;
      header->dwStreamNameSize = 0;
      WriteArchive(Buffer, HEADER_SIZE);

      if (bVerbose)
	oem_printf(stderr,
		   ", link to '%2!.*ws!': %1!u! bytes%%n",
		   HEADER_SIZE + header->Size.LowPart,
		   LinkName->Length >> 1, LinkName->Buffer);

      WriteArchive((LPBYTE) LinkName->Buffer, LinkName->Length);

      ++dwFileCounter;
      return true;
    }

  bool bResult = ReadFileStreamsToArchive(File, hFile);

  if (bResult)
    if (((BackupMethod == BACKUP_METHOD_FULL) |
	 (BackupMethod == BACKUP_METHOD_INC)) &
	((file_info.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0))
      {
	FILE_BASIC_INFORMATION basic_info = { 0 };
	basic_info.FileAttributes =
	  file_info.dwFileAttributes & ~FILE_ATTRIBUTE_ARCHIVE;

	IO_STATUS_BLOCK io_status;
	NTSTATUS status =
	  NtSetInformationFile(hFile,
			       &io_status,
			       &basic_info,
			       sizeof basic_info,
			       FileBasicInformation);

	if (!NT_SUCCESS(status))
	  {
	    WErrMsgA errmsg(RtlNtStatusToDosError(status));
	    oem_printf(stderr,
		       "strarc: Error resetting archive attribute on "
		       "%2!.*ws!: %1%%n",
		       errmsg,
		       File->Length >> 1, File->Buffer);
	  }
      }

  NtClose(hFile);

  return bResult;
}

void
StrArc::BackupDirectory(PUNICODE_STRING Path, HANDLE Handle)
{
  UNICODE_STRING base_path = *Path;

  if ((base_path.Length == 2) &
      (base_path.Buffer[0] == L'.'))
    base_path.Length = 0;
  else if (base_path.Length > 0)
    {
      NTSTATUS status = RtlAppendUnicodeToString(&base_path, L"\\");

      if (!NT_SUCCESS(status))
	{
	  oem_printf(stderr,
		     "strarc: Path is too long: '%1!.*ws!'%%n",
		     Path->Length >> 1, Path->Buffer);
	  return;
	}
    }

  BYTE short_name_buffer[sizeof(finddata.ShortName)];
  UNICODE_STRING short_name = { 0 };
  short_name.MaximumLength = sizeof(short_name_buffer);
  short_name.Buffer = (PWSTR) short_name_buffer;

  for (finddata.FindFirst(Handle);
       ;
       finddata.FindNext(Handle))
    {
      YieldSingleProcessor();

      if (!finddata)
	{
	  DWORD dwLastError = finddata.GetLastError();
	  if (dwLastError == ERROR_NO_MORE_FILES)
	    break;
	  else
	    {
	      WErrMsgA errmsg(dwLastError);
	      oem_printf(stderr,
			 "strarc: Error reading directory '%2!.*ws!': %1%%n",
			 errmsg,
			 Path->Length >> 1, Path->Buffer);

	      return;
	    }
	}

      if ((finddata.FileNameLength == 2) &
	  (finddata.FileName[0] == L'.'))
	continue;
      if ((finddata.FileNameLength == 4) &
	  (finddata.FileName[0] == L'.') &
	  (finddata.FileName[1] == L'.'))
	continue;

      if (bCancel)
	return;

      if (finddata.FileNameLength > USHORT_MAX)
	{
	  oem_printf(stderr,
		     "strarc: Path is too long: %1!.*ws!",
		     Path->Length >> 1, Path->Buffer);

	  oem_printf(stderr,
		     "\\%1!.*ws!%%n",
		     finddata.FileNameLength >> 1, finddata.FileName);

	  continue;
	}

      UNICODE_STRING entry_name;
      InitCountedUnicodeString(&entry_name,
			       finddata.FileName,
			       (USHORT) finddata.FileNameLength);

      UNICODE_STRING name = base_path;

      NTSTATUS status =
	RtlAppendUnicodeStringToString(&name,
				       &entry_name);

      if (!NT_SUCCESS(status))
	{
	  oem_printf(stderr,
		     "strarc: Path is too long: %1!.*ws!",
		     Path->Length >> 1, Path->Buffer);

	  oem_printf(stderr,
		     "\\%1!.*ws!%%n",
		     finddata.FileNameLength >> 1, finddata.FileName);

	  continue;
	}

      short_name.Length = finddata.ShortNameLength;
      if (finddata.ShortNameLength > 0)
	memcpy(short_name.Buffer,
	       finddata.ShortName,
	       finddata.ShortNameLength);

      BackupFile(&name, &short_name, true);
    }
}
