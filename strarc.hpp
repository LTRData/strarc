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

// This is the backup stream buffer size used in API calls. See description of
// the -b command line switch for details.
#ifndef DEFAULT_STREAM_BUFFER_SIZE
#define DEFAULT_STREAM_BUFFER_SIZE (128 << 10)
#endif

#ifndef USHORT_MAX
#define USHORT_MAX INTSAFE_USHORT_MAX
#endif

// This is the size in bytes of the WIN32_STREAM_ID header without any of the
// actual backed up data.
#define HEADER_SIZE 20

#include <ntfileio.hpp>
#include <spsleep.h>

#include "linktrack.hpp"

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

enum BackupMethods
  {
    BACKUP_METHOD_COPY,
    BACKUP_METHOD_FULL,
    BACKUP_METHOD_DIFF,
    BACKUP_METHOD_INC
  };

LPSTR GetStreamIdDescription(DWORD StreamId);

LPSTR GetStreamAttributesDescription(DWORD StreamId);

#ifdef _WIN64
#define MEMBERCALL
#else
#define MEMBERCALL __stdcall
#endif

//class StrArc;

class StrArc
{

private:

  WCHAR wczFullPathBuffer[32769];

  // Handle to the open archive the program is working with.
  HANDLE hArchive;

  // Handle to root directory of current backup or restore operation. Usually
  // set to NtCurrentDirectoryHandle() to make it same root directory as
  // current directory used in Win32 API calls.
  HANDLE RootDirectory;

  // This buffer is used as a temporary storage for different purposes, mainly
  // to store full path to the current directory when backing up or restoring
  // the "." node in the current directory.
  // Size needs to be larger than USHORT_MAX bytes (at least 32768 Unicode
  // characters).
  UNICODE_STRING FullPath;

  // Flags set by wmain() to corresponding command line switches.
  bool bVerbose;
  bool bListFiles;
  DWORD dwFileCounter;
  bool bLocal;
  bool bTestMode;
  bool bListOnly;
  BOOL bProcessSecurity;
  bool bProcessFileTimes;
  bool bProcessFileAttribs;
  bool bHardLinkSupport;
  bool bRestoreCompression;
  bool bOverwriteOlder;
  bool bOverwriteArchived;
  bool bFreshenExisting;
  DWORD dwExtractCreation;
  bool bRestoreShortNamesOnly;
  bool bBackupRegistrySnapshots;

  // This is the buffer used when calling the backup API functions.
  LPBYTE Buffer;
  DWORD dwBufferSize;

  // Because the buffer begins with a WIN32_STREAM_ID structure, this property
  // helps manipulating the header fields in the buffer.
  __declspec(property(get = GetHeader)) LPWIN32_STREAM_ID header;

  LPWIN32_STREAM_ID GetHeader()
  {
    return (LPWIN32_STREAM_ID) Buffer;
  }

  DWORD GetDataOffset()
  {
    return HEADER_SIZE + header->dwStreamNameSize;
  }

  // Backup method for this session
  BackupMethods BackupMethod;

  // Strings used by ExcludedString method
  DWORD dwExcludeStrings;
  LPWSTR szExcludeStrings;
  DWORD dwIncludeStrings;
  LPWSTR szIncludeStrings;

  // 256 slot hash table for hard link tracker
  LinkTrackerItem *LinkTrackerItems[256];

  // Buffer object that holds data from last directory list operation.
  NtFileFinder finddata;

  void
  MEMBERCALL
  FillEntireBuffer(DWORD &dwBytesRead,
		   PLARGE_INTEGER BytesToRead);

  bool
  MEMBERCALL
  WriteHardLinkFromArchive(HANDLE &hFile,
			   DWORD dwBytesRead,
			   PUNICODE_STRING Target,
			   bool bSeekOnly);

  bool
  MEMBERCALL
  WriteFileFromArchive(HANDLE &hFile,
		       DWORD dwBytesRead,
		       PLARGE_INTEGER BytesToRead,
		       UNICODE_STRING File,
		       bool &bSeekOnly);

  bool
  MEMBERCALL
  WriteAlternateStreamFromArchive(DWORD dwBytesRead,
				  PLARGE_INTEGER BytesToRead,
				  ULONG StreamAttributes,
				  PUNICODE_STRING FileBaseName,
				  bool bSeekOnly);

  /* File         - Name of file to restore with path relative to working
   *                directory. File name must not be empty
   * FileInfo     - Structure with file attributes and file times to restore.
   * ShortName    - The 8.3 name to restore (max 13 characters, without path).
                    This parameter can be NULL or point to an empty string if
		    no short name is to be restored.
  */
  bool
  MEMBERCALL
  RestoreFile(PUNICODE_STRING File,
	      const BY_HANDLE_FILE_INFORMATION * FileInfo,
	      PUNICODE_STRING ShortName);

  void
  MEMBERCALL
  BackupDirectoryTree();

  void
  MEMBERCALL
  RestoreDirectoryTree();

  void
  MEMBERCALL
  BackupDirectory(PUNICODE_STRING Path,
		  HANDLE Handle);

  bool
  MEMBERCALL
  BackupFile(PUNICODE_STRING File,
	     PUNICODE_STRING ShortName,
	     bool bTraverseDirectories);

  void
  MEMBERCALL
  CreateRegistrySnapshots();

  bool
  IsNewFileHeader()
  {
    if ((header->dwStreamId == BACKUP_INVALID) &&
	(header->dwStreamAttributes == STRARC_MAGIC) &&
	((header->Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION) |
	 (header->Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION + 26))
	&& (header->dwStreamNameSize > 0)
	&& (header->dwStreamNameSize <= 65535))
      return true;

    if (header->dwStreamNameSize & 1)
      return true;

    return false;
  }

  PUNICODE_STRING
  MatchLink(DWORD dwVolumeSerialNumber,
	    LONGLONG NodeNumber,
	    PUNICODE_STRING Name)
  {
    PUNICODE_STRING LinkName = NULL;

    for (LinkTrackerItem * linkinfo =
	   LinkTrackerItems[(NodeNumber & 0xFF0) >> 4];
	 linkinfo != NULL;
	 linkinfo = linkinfo->GetNext())
      {
	YieldSingleProcessor();

	if (linkinfo->Match(dwVolumeSerialNumber, NodeNumber, &LinkName))
	  return LinkName;
      }

    LinkTrackerItem *NewLinkInfo =
      LinkTrackerItem::NewItem(LinkTrackerItems[(NodeNumber & 0xFF0) >> 4],
			       Name,
			       dwVolumeSerialNumber,
			       NodeNumber);

    if (NewLinkInfo == NULL)
      status_exit(XE_NOT_ENOUGH_MEMORY_FOR_LINK_TRACKER);

    LinkTrackerItems[(NodeNumber & 0xFF0) >> 4] = NewLinkInfo;

    return NULL;
  }

  // This function examines all paths and filenames on behalf of the backup and
  // restore routines. It identifies names containing strings matching the -i
  // or -e switches on the command line and returns true if this name is ok to
  // process.
  // If IncludeIfNotExcluded is set to true, this function only checks for
  // explicit exclusions and returns false unless an exclusion string is
  // matched. Otherwise, this functions continues with comparing to inclusion
  // strings and if any such strings exist and path does not match any of them,
  // true is returned.
  bool
  ExcludedString(PUNICODE_STRING Path, bool IncludeIfNotExcluded)
  {
    // This routine does not handle empty strings correctly.
    if (Path->Length == 0)
      return false;

    LPCWSTR wcPathEnd = Path->Buffer + (Path->Length >> 1);

    if (dwExcludeStrings != 0)
      {
	DWORD dwExcludeString = dwExcludeStrings;
	LPCWSTR wczExcludeString = szExcludeStrings;

	do
	  {
	    DWORD_PTR dwExclStrLen = wcslen(wczExcludeString);

	    for (LPCWSTR wcPathPtr = Path->Buffer;
		 (DWORD) (wcPathEnd - wcPathPtr) >= dwExclStrLen;
		 wcPathPtr++)
	      if (wcsnicmp(wcPathPtr, wczExcludeString, dwExclStrLen) == 0)
		return true;

	    wczExcludeString += dwExclStrLen + 1;
	  }
	while (--dwExcludeString);
      }

    if ((dwIncludeStrings == 0) | IncludeIfNotExcluded)
      return false;

    DWORD dwIncludeString = dwIncludeStrings;
    LPCWSTR wczIncludeString = szIncludeStrings;

    do
      {
	DWORD_PTR dwInclStrLen = wcslen(wczIncludeString);

	for (LPCWSTR wcPathPtr = Path->Buffer;
	     (DWORD) (wcPathEnd - wcPathPtr) >= dwInclStrLen;
	     wcPathPtr++)
	  if (wcsnicmp(wcPathPtr, wczIncludeString, dwInclStrLen) == 0)
	    return false;

	wczIncludeString += dwInclStrLen + 1;
      }
    while (--dwIncludeString);

    return true;
  }

  bool
  MEMBERCALL
  BackupCopyFile(HANDLE SourceHandle,
		 HANDLE TargetHandle,
		 PUNICODE_STRING SourceName,
		 PUNICODE_STRING TargetName);

  // This function reads up to the specified block size from the archive. If
  // EOF, it returns the number of bytes actually read.
  DWORD
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

  // This function reads a header for a new file from the archive. If not a
  // valid header is found the function seeks forward in the archive until it
  // finds a valid header or EOF. If a valid header is found, true is returned,
  // otherwise false is returned.
  bool
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
	      GetStreamIdDescription(header->dwStreamId),
	      GetStreamAttributesDescription(header->dwStreamAttributes),
	      header->dwStreamNameSize,
	      header->Size.HighPart, header->Size.LowPart);
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

  // This function writes a specified block to the archive. If it is not
  // possible/ to write the complete block that is considered a fatal error and
  // status_exit() is called causing the application to  display an error
  // message and exit.
  void
  WriteArchive(LPBYTE lpBuf, DWORD dwSize)
  {
    DWORD dwBytesWritten;

    while (dwSize > 0)
      if (!WriteFile(hArchive, lpBuf, dwSize, &dwBytesWritten, NULL))
	status_exit(XE_ARCHIVE_IO);
      else if (dwBytesWritten == 0)
	{
	  if (bVerbose)
	    fputs("\r\nWrite of archive raised EOF condition.\r\n", stderr);

	  status_exit(XE_ARCHIVE_TRUNC);
	}
      else
	{
	  dwSize -= dwBytesWritten;
	  lpBuf += dwBytesWritten;
	}
  }

  // This function skips forward in current archive, using current buffer. If
  // cancelled, it returns false, otherwise true.
  bool
  SkipArchive(PLARGE_INTEGER BytesToRead)
  {
    DWORD dwBytesRead;

    while (BytesToRead->QuadPart > 0)
      {
	YieldSingleProcessor();

	if (bCancel)
	  return false;

	dwBytesRead = ReadArchive(Buffer,
				  BytesToRead->QuadPart >
				  (LONGLONG) dwBufferSize ?
				  dwBufferSize : BytesToRead->LowPart);

	BytesToRead->QuadPart -= dwBytesRead;

	if (dwBytesRead == 0)
	  {
	    if (bVerbose)
	      fprintf(stderr,
		      ", %.4g %s missing (%s:%u).\r\n",
		      TO_h(BytesToRead->QuadPart),
		      TO_p(BytesToRead->QuadPart),
		      __FILE__, __LINE__);
	    status_exit(XE_ARCHIVE_TRUNC);
	  }
      }

    return true;
  }

  bool
  MEMBERCALL
  InitializeBuffer(DWORD dwSize)
  {
    dwBufferSize = dwSize;
    Buffer = (LPBYTE) LocalAlloc(LPTR, dwBufferSize);
    return Buffer != NULL;
  }

  void
  __declspec(noreturn) status_exit(XError XE, LPCWSTR Name = NULL);

public:

  StrArc()
  {
    memset(this, 0, sizeof *this);

    dwFileCounter = 0;
    FullPath.Length = 0;
    FullPath.MaximumLength = USHORT_MAX;
    FullPath.Buffer = wczFullPathBuffer;
    hArchive = INVALID_HANDLE_VALUE;
    bCancel = false;
    bVerbose = false;
    bLocal = false;
    bTestMode = false;
    bListOnly = false;
    bListFiles = false;
    bProcessSecurity = TRUE;
    bProcessFileTimes = true;
    bProcessFileAttribs = true;
    bHardLinkSupport = true;
    bRestoreCompression = true;
    bOverwriteOlder = false;
    bOverwriteArchived = false;
    bFreshenExisting = false;
    dwExtractCreation = FILE_CREATE;
    bRestoreShortNamesOnly = false;
    bBackupRegistrySnapshots = false;

    BackupMethod = BACKUP_METHOD_COPY;

    dwExcludeStrings = 0;
    szExcludeStrings = NULL;
    dwIncludeStrings = 0;
    szIncludeStrings = NULL;

    dwBufferSize = DEFAULT_STREAM_BUFFER_SIZE;

    Buffer = NULL;
  }

  ~StrArc()
  {
    for (int i = 0; i < 256; i++)
      for (LinkTrackerItem *item = LinkTrackerItems[i];
	   item != NULL;
	   item = item->DeleteAndGetNext());

    if (Buffer != NULL)
      LocalFree(Buffer);

    if (RootDirectory != NULL)
      NtClose(RootDirectory);

    if (hArchive != NULL)
      CloseHandle(hArchive);
  }

  bool bCancel;

  int
  MEMBERCALL
  Main(int argc, LPWSTR *argv);

  StrArc *
  MEMBERCALL
  TemplateNew(HANDLE Archive)
  {
    StrArc *cloned = new StrArc;

    if (cloned == NULL)
      return NULL;

    *cloned = *this;
    memset(cloned->LinkTrackerItems,
	   0,
	   sizeof(cloned->LinkTrackerItems));

    cloned->Buffer = NULL;
    cloned->RootDirectory = NULL;
    cloned->hArchive = NULL;

    if (!cloned->InitializeBuffer(cloned->dwBufferSize))
      {
	delete cloned;
	return NULL;
      }

    if (!DuplicateHandle(GetCurrentProcess(),
			 RootDirectory,
			 GetCurrentProcess(),
			 &cloned->RootDirectory,
			 0,
			 FALSE,
			 DUPLICATE_SAME_ACCESS))
      {
	delete cloned;
	return NULL;
      }

    cloned->hArchive = Archive;

    return cloned;
  }

  bool
  MEMBERCALL
  ReadFileStreamsToArchive(PUNICODE_STRING File,
			   HANDLE hFile);

  bool
  MEMBERCALL
  WriteFileStreamsFromArchive(PUNICODE_STRING File,
			      HANDLE &hFile,
			      bool &bSeekOnly);

};
