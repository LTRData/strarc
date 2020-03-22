/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2006
 *
 * strarc.hpp
 * Declarations of common variables and functions.
 */

// This magic is the Stream Attributes field in the headers specifying that a
// new file begins in the archive.
#define STRARC_MAGIC 0xBAC00001

// This is the extension added to the registry database snapshot files created
// when backing up with the -r switch.
#define REGISTRY_SNAPSHOT_FILE_EXTENSION L".$sards"

extern HANDLE RootDirectory;

// Handle to the open archive the program is working with.
extern HANDLE hArchive;

// On backup operation, this buffer contains the relative path from the
// current directory to the directory/file currently backed up.
extern WCHAR wczCurrentPath[32768];

// This buffer is used as a temporary storage for different purposes, mainly
// to store full path to the current directory when backing up or restoring the
// "." node in the current directory.
extern WCHAR wczFullPathBuffer[32769];

// Various flags set by wmain() to corresponding command line switches.
extern bool bVerbose;
extern bool bListFiles;
extern DWORD dwFileCounter;
extern bool bCancel;
extern bool bLocal;
extern bool bTestMode;
extern BOOL bProcessSecurity;
extern bool bProcessFileTimes;
extern bool bProcessFileAttribs;
extern bool bHardLinkSupport;
extern bool bRestoreCompression;
extern bool bOverwriteOlder;
extern bool bOverwriteArchived;
extern bool bFreshenExisting;
extern DWORD dwExtractCreation;
extern bool bRestoreShortNamesOnly;
extern bool bBackupRegistrySnapshots;

// This is the buffer used when calling the backup API functions.
extern LPBYTE Buffer;
extern DWORD dwBufferSize;
// Because the buffer begins with a WIN32_STREAM_ID structure, this macro helps
// manipulating the header fields in the buffer.
#define header (*(WIN32_STREAM_ID*)Buffer)
// This is the size in bytes of the WIN32_STREAM_ID header without any of the
// actual backed up data.
#define HEADER_SIZE 20

// This enumeration specifies various error codes that could be used when
// calling status_exit() on non-recoverable errors.
enum XError
{
  XE_NOERROR,
  XE_CANCELLED,
  XE_CREATE_DIR,
  XE_CHANGE_DIR,
  XE_CREATE_FILE,
  XE_FILE_IO,
  XE_ARCHIVE_IO,
  XE_ARCHIVE_OPEN,
  XE_CREATE_PIPE,
  XE_FILTER_EXECUTE,
  XE_ARCHIVE_TRUNC,
  XE_ARCHIVE_BAD_HEADER,
  XE_NOT_WINDOWSNT,
  XE_TOO_LONG_PATH,
  XE_NOT_ENOUGH_MEMORY,
  XE_NOT_ENOUGH_MEMORY_FOR_LINK_TRACKER,
  XE_BAD_BUFFER
};

extern enum BackupMethods
{
  BACKUP_METHOD_COPY,
  BACKUP_METHOD_FULL,
  BACKUP_METHOD_DIFF,
  BACKUP_METHOD_INC
} BackupMethod;

void
__declspec(noreturn)
status_exit(XError XE, LPCWSTR Name = NULL);

void BackupDirectory(LPWSTR wczPathPtr);
void BackupDirectoryTree();
void RestoreDirectoryTree();

bool BackupFile(LPWSTR wczFile, LPCWSTR wczShortName);
bool RestoreFile(LPCWSTR wczFile,
		 const BY_HANDLE_FILE_INFORMATION * FileInfo,
		 LPCWSTR wczShortName);
bool ExcludedString(LPCWSTR wczPath);
bool CreateDirectoryPath(LPCWSTR wczPath);

void CreateRegistrySnapshots();

char *GetStreamIdDescription(DWORD StreamId);

char *GetStreamAttributesDescription(DWORD StreamId);

// This function reads up to the specified block size from the archive. If EOF,
// it returns the number of bytes actually read.
inline DWORD
ReadArchive(LPBYTE lpBuf, DWORD dwSize)
{
  DWORD dwBytesRead;
  DWORD dwTotalBytes = 0;

  while (dwSize > 0)
    {
      if (!ReadFile(hArchive, lpBuf, dwSize, &dwBytesRead, NULL))
	switch (GetLastError())
	  {
	  case ERROR_OPERATION_ABORTED:
	  case ERROR_NETNAME_DELETED:
	  case ERROR_BROKEN_PIPE:
	  case ERROR_INVALID_HANDLE:
	    dwBytesRead = 0;
	    break;
	  default:
	    status_exit(XE_ARCHIVE_IO);
	  }

      if (dwBytesRead == 0)
	return dwTotalBytes;

      dwTotalBytes += dwBytesRead;
      dwSize -= dwBytesRead;
      lpBuf += dwBytesRead;
    }

  return dwTotalBytes;
}

// This function reads a header for a new file from the archive. If not a valid
// header is found the function seeks forward in the archive until it finds a
// valid header or EOF. If a valid header is found, true is returned, otherwise
// false is returned.
inline bool
ReadArchiveHeader(WIN32_STREAM_ID * lpBuf)
{
  DWORD dwBytesRead = ReadArchive((LPBYTE) lpBuf, HEADER_SIZE);

  if (dwBytesRead < HEADER_SIZE)
    return false;

  if ((lpBuf->dwStreamId == BACKUP_INVALID) &
      (lpBuf->dwStreamAttributes == STRARC_MAGIC) &
      ((lpBuf->Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION) |
       (lpBuf->Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION + 26)) &
      (lpBuf->dwStreamNameSize > 0) &
      (lpBuf->dwStreamNameSize <= 65535) &
      ((lpBuf->dwStreamNameSize & 1) == 0))
    return true;

  if (bVerbose)
    fprintf(stderr, "strarc: Invalid header "
	    "[id=%s, attr=%s, %u bytes name, size=0x%.8x%.8x], seeking...\n",
	    GetStreamIdDescription(header.dwStreamId),
	    GetStreamAttributesDescription(header.dwStreamAttributes),
	    header.dwStreamNameSize,
	    header.Size.HighPart, header.Size.LowPart);
  else
    fputs("strarc: Error in archive, skipping to next valid header...\r\n",
	  stderr);

  do
    {
      YieldSingleProcessor();

      if (bCancel)
	return false;

      MoveMemory(lpBuf, ((LPBYTE) lpBuf) + 1, HEADER_SIZE - 1);
      if (ReadArchive(((LPBYTE) lpBuf) + HEADER_SIZE - 1, 1) != 1)
	{
	  fputs("strarc: End of archive.\r\n", stderr);
	  return false;
	}
    }
  while ((lpBuf->dwStreamId != BACKUP_INVALID) |
	 (lpBuf->dwStreamAttributes != STRARC_MAGIC) |
	 ((lpBuf->Size.QuadPart != sizeof BY_HANDLE_FILE_INFORMATION) &
	  (lpBuf->Size.QuadPart != sizeof BY_HANDLE_FILE_INFORMATION + 26)) |
	 (lpBuf->dwStreamNameSize == 0) |
	 (lpBuf->dwStreamNameSize > 65535) | (lpBuf->dwStreamNameSize & 1));

  return true;
}

// This function writes a specified block to the archive. If it is not possible
// to write the complete block that is considered a fatal error and
//  status_exit() is called causing the application to  display an error
//  message and exit.
inline void
WriteArchive(LPBYTE lpBuf, DWORD dwSize)
{
  DWORD dwBytesWritten;

  while (dwSize > 0)
    {
      if (!WriteFile(hArchive, lpBuf, dwSize, &dwBytesWritten, NULL))
	status_exit(XE_ARCHIVE_IO);

      if (dwBytesWritten == 0)
	{
	  if (bVerbose)
	    fputs("\r\nWrite of archive raised EOF condition.\r\n", stderr);

	  status_exit(XE_ARCHIVE_TRUNC);
	}

      dwSize -= dwBytesWritten;
      lpBuf += dwBytesWritten;
    }
}

// This function skips forward in current archive, using current buffer. If
// cancelled, it returns false, otherwise true.
inline bool
SkipArchive(PULARGE_INTEGER BytesToRead)
{
  DWORD dwBytesRead;

  while (BytesToRead->QuadPart > 0)
    {
      YieldSingleProcessor();

      if (bCancel)
	return false;

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
    }

  return true;
}
