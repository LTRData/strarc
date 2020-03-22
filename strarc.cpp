/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2009
 *
 * strarc.cpp
 * Main source file. This contains the startup wmain() function, command line
 * argument parsing and some functions used by both backup and restore
 * routines.
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

// This is the backup stream buffer size used in API calls. See description of
// the -b command line switch for details.
#ifndef DEFAULT_STREAM_BUFFER_SIZE
#define DEFAULT_STREAM_BUFFER_SIZE (128 << 10)
#endif

// Definitions of common variables declared in strarc.hpp. No further
// description here.
DWORD dwFileCounter = 0;
WCHAR wczCurrentPath[32768] = L"";
WCHAR wczFullPathBuffer[32769] = L"";
HANDLE hArchive = INVALID_HANDLE_VALUE;
bool bCancel = false;
bool bVerbose = false;
bool bLocal = false;
bool bBackupMode = false;
bool bRestoreMode = false;
bool bTestMode = false;
bool bListFiles = false;
BOOL bProcessSecurity = TRUE;
bool bProcessFileTimes = true;
bool bProcessFileAttribs = true;
bool bHardLinkSupport = true;
bool bRestoreCompression = true;
bool bFilesFromStdIn = false;
bool bFilesFromStdInUnicode = false;
bool bOverwriteOlder = false;
bool bOverwriteArchived = false;
bool bFreshenExisting = false;
DWORD dwArchiveCreation = CREATE_ALWAYS;
DWORD dwExtractCreation = CREATE_NEW;
bool bRestoreShortNamesOnly = false;
bool bBackupRegistrySnapshots = false;

BackupMethods BackupMethod = BACKUP_METHOD_COPY;

DWORD dwExcludeStrings = 0;
LPWSTR szExcludeStrings = NULL;
DWORD dwIncludeStrings = 0;
LPWSTR szIncludeStrings = NULL;
DWORD dwBufferSize = DEFAULT_STREAM_BUFFER_SIZE;

LPWSTR wczFilterCmd = NULL;

LPBYTE Buffer = NULL;

void
usage()
{
  fprintf(stderr,
	  "Backup Stream archive I/O Utility, version 0.1.5\r\n"
	  "Build date: " __DATE__
	  ", Copyright (C) Olof Lagerkvist 2004-2009\r\n"
	  "http://www.ltr-data.se      olof@ltr-data.se\r\n"
	  "\n"
	  "Usage:\r\n"
	  "\n"
	  "strarc -c[afj] [-z:CMD] [-m:f|d|i] [-l|v] [-s:ls] [-b:SIZE] [-e:EXCLUDE[,...]]\r\n"
	  "       [-i:INCLUDE[,...]] [-d:DIR] [ARCHIVE] [LIST ...]\r\n"
	  "\n"
	  "strarc -x [-8] [-z:CMD] [-l|v] [-s:aclst] [-o[:afn]] [-b:SIZE]\r\n"
	  "       [-e:EXCLUDE[,...]] [-i:INCLUDE[,...]] [-d:DIR] [ARCHIVE]\r\n"
	  "\n"
	  "strarc -t [-z:CMD] [-v] [-b:SIZE] [-e:EXCLUDE[,...]] [-i:INCLUDE[,...]]\r\n"
	  "       [ARCHIVE]\r\n" "\n" "-- Main options --\r\n"
	  "\n"
	  "-c     Backup operation. Default archive output is stdout. If an archive\r\n"
	  "       filename is given, that file is overwritten if not the -a switch is also\r\n"
	  "       specified.\r\n" "\n"
	  "-x     Restore operation. Default archive input is stdin.\r\n" "\n"
	  "-t     Read archive and display filenames and possible errors but no\r\n"
	  "       extracting. Default archive input is stdin.\r\n" "\n"
	  "-- Backup options --\r\n" "\n"
	  "-a     Append to existing archive.\r\n" "\n"
	  "-f     Read files to backup from stdin. If this flag is not present the tree of\r\n"
	  "       the current directory (or directory specified with -d) is backed up\r\n"
	  "       including all files and subdirectories in it. The -e and -i switches are\r\n"
	  "       compatible with -f so that you can filter out which files from the list\r\n"
	  "       read from stdin should be backed up. The filenames read from stdin are\r\n"
	  "       read as Ansi characters if -f is given, or Unicode characters if -F is\r\n"
	  "       given. You may get better performance with the -F switch than with the\r\n"
	  "       -f switch.\r\n" "\n"
	  "-j     Do not follow junctions or other reparse points, instead the reparse\r\n"
	  "       points are backed up.\r\n" "\n"
	  "-m     Select backup method.\r\n"
	  "       f - Full. All files and directories are backed up and any archive\r\n"
	  "           attributes are cleared on the backed up files.\r\n"
	  "       d - Differential. All files and directories with their archive\r\n"
	  "           attributes set are backed up but the archive attributes are not\r\n"
	  "           cleared. This effectively means to backup everything changed since\r\n"
	  "           the last full or incremental backup.\r\n"
	  "       i - Incremental. All files and directories with their archive attributes\r\n"
	  "           set are backed up and the archive attributes are cleared on the\r\n"
	  "           backed up files. This effectively means to backup all files changed\r\n"
	  "           since the last full or incremental backup.\r\n"
	  "\n"
	  "-r     Backup loaded registry database of the running system.\r\n"
	  "\n"
	  "       Creates temporary snapshot files of loaded registry database files and\r\n"
	  "       stores them in the backup archive. This is recommended when creating a\r\n"
	  "       complete backup of the system drive on a running system. When the backup\r\n"
	  "       archive is restored to a new drive, the backed up snapshots will be\r\n"
	  "       extracted to the locations where Windows expects the registry database\r\n"
	  "       files.\r\n"
	  "\n"
	  "-- Restore options --\r\n" "\n"
	  "-o     Overwrite existing files.\r\n" "\n"
	  "-o:a   Overwrite existing files without archive attribute set. This is\r\n"
	  "       recommended when restoring from incremental backup sets so that files\r\n"
	  "       restored from the full backup can be overwritten by later versions\r\n"
	  "       incremental backup archives without overwriting files changed on disk\r\n"
	  "       since the last backup job.\r\n" "\n"
	  "-o:n   Overwrite existing files only when files in archive are newer.\r\n"
	  "\n"
	  "-o:an  Logical 'and' combination of the above so that only files older and\r\n"
	  "       without archive attributes are overwritten.\r\n" "\n"
	  "-o:f   Only extract files that already exists on disk and is newer in the\r\n"
	  "       archive (freshen existing files).\r\n" "\n"
	  "-o:af  Logical 'and' combination of -o:a and -o:f so that only files existing\r\n"
	  "       on disk without archive attribute and older than corresponding files in\r\n"
	  "       the archive are overwritten.\r\n" "\n"
	  "-8     Create restored files using their saved 8.3 compatibility name. This is\r\n"
	  "       ignored for directories.\r\n" "\n"
	  "-- Options available both for backup and restore operations --\r\n"
	  "\n"
	  "-b     Specifies the buffer size used when calling the backup API functions.\r\n"
	  "       You can suffix the number with K or M to specify KB or MB. The default\r\n"
	  "       value is %.4g %s. The specified size must be at least 64 KB.\r\n"
	  "\n"
	  "-d     Before doing anything, change to this directory. When extracting, the\r\n"
	  "       directory is first created if it does not exist.\r\n" "\n"
	  "-l     Display filenames like -t while backing up/extracting.\r\n"
	  "\n"
	  "-s     Ignore/skip restoring some information while backing up/restoring:\r\n"
	  "       a - No file attributes restored.\r\n"
	  "       c - Skip restoring the 'compressed' and 'sparse' attributes on extracted\r\n"
	  "           files\r\n"
	  "       l - On backup: No hard link tracking, archive all links as separate\r\n"
	  "           files.\r\n"
	  "           On restore: Create separate files when extracting hard linked files\r\n"
	  "           instead of first attempt to restore the hard link between them.\r\n"
	  "       s - No security (access/owner/audit) information backed up/restored.\r\n"
	  "       t - No file creation/last access/last written times restored.\r\n"
	  "\n" "-- General options --\r\n" "\n"
	  "-e     Exclude paths and files matching any string in the list.\r\n"
	  "\n"
	  "-i     Include only paths and files matching any string in the list.\r\n"
	  "       Default is to include all files and directories. -e takes presedence\r\n"
	  "       over -i.\r\n" "\n"
	  "-v     Verbose debug mode to stderr. Useful to find out how strarc handles\r\n"
	  "       errors in filesystems and archives.\r\n" "\n"
	  "-z     Filter archive I/O through another program, e.g. a compression utility.\r\n"
	  "\n" "-- Archive name, paths and filenames --\r\n" "\n"
	  "ARCHIVE   Name of the archive file, stdin/stdout is default. To specify stdout\r\n"
	  "          as output when you also specify a LIST, specify - or \"\" as archive\r\n"
	  "          name.\r\n" "\n"
	  "LIST      List of files and directories to archive. If not given, the current\r\n"
	  "          directory is archived.\r\n" "\n"
	  "For further information about this program, license terms, known bugs and\r\n"
	  "important limitation notes when used as a backup application, how-tos, FAQ and\r\n"
	  "usage examples, please read the file strarc.txt following this program file or\r\n"
	  "download strarc.zip from http://www.ltr-data.se/opencode.html where the latest\r\n"
	  "version should be available.\r\n", TO_h(DEFAULT_STREAM_BUFFER_SIZE),
	  TO_p(DEFAULT_STREAM_BUFFER_SIZE));

  exit(1);
}

BOOL WINAPI
ConsoleCtrlHandler(DWORD dwCtrlType)
{
  switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      bCancel = true;
      return TRUE;

    default:
      return FALSE;
    }
}

// This function is called on different kinds of non-recoverable errors. It
// displays an error message and exits the program.
void
__declspec(noreturn)
status_exit(XError XE, LPCWSTR Name)
{
  WErrMsgA syserrmsg;
  bool bDisplaySysErrMsg = false;
  const char *errmsg = "";

  switch (XE)
    {
    case XE_CANCELLED:
      errmsg = "\r\nstrarc aborted: Operation cancelled.\r\n";
      break;
    case XE_CREATE_DIR:
      errmsg = "\r\nstrarc aborted: Cannot create directory.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_CHANGE_DIR:
      errmsg = "\r\nstrarc aborted: Cannot change to directory.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_CREATE_FILE:
      errmsg = "\r\nstrarc aborted: Cannot create file.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_FILE_IO:
      errmsg = "\r\nstrarc aborted: File I/O error.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_ARCHIVE_IO:
      errmsg = "\r\nstrarc aborted: Archive I/O error.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_ARCHIVE_OPEN:
      errmsg = "\r\nstrarc: Archive open error.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_CREATE_PIPE:
      errmsg = "\r\nstrarc: Cannot create pipe.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_FILTER_EXECUTE:
      errmsg = "\r\nstrarc: Cannot execute filter.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_ARCHIVE_TRUNC:
      errmsg = "\r\nstrarc aborted: Unexpected end of archive.\r\n";
      break;
    case XE_ARCHIVE_BAD_HEADER:
      errmsg = "\r\nstrarc aborted: Bad archive format, aborted.\r\n";
      break;
    case XE_NOT_WINDOWSNT:
      MessageBoxA(NULL, "This program requires Windows NT.", "strarc",
		  MB_ICONERROR);
      exit(XE);
    case XE_TOO_LONG_PATH:
      errmsg = "\r\nstrarc aborted: Target path is too long.\r\n";
      break;
    case XE_NOT_ENOUGH_MEMORY:
      errmsg = "\r\nstrarc aborted: Memory allocation failed.\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_NOT_ENOUGH_MEMORY_FOR_LINK_TRACKER:
      errmsg =
	"\r\n"
	"strarc aborted: Memory allocation for hard link tracking failed.\r\n"
	"Free some memory or use the -s:l option\r\n";
      bDisplaySysErrMsg = true;
      break;
    case XE_BAD_BUFFER:
      errmsg = "\r\nstrarc aborted: The stream buffer size is too small.\r\n";
      break;
    }

  if (errmsg)
    fputs(errmsg, stderr);
  if (bDisplaySysErrMsg)
    if (Name != NULL)
      win_perror(Name);
    else
      oem_printf(stderr, "%1%%n", syserrmsg);

  if (bVerbose)
    fprintf(stderr, "\nstrarc aborted, %u files processed.\n", dwFileCounter);

  exit(XE);
}

// This function enables backup and restore priviliges for this process. If
// the priviliges are not granted that is silently ignored.
BOOL
EnableBackupPrivileges()
{
  HANDLE hToken;
  BYTE buf[sizeof(TOKEN_PRIVILEGES) * 2];
  TOKEN_PRIVILEGES *tkp = (TOKEN_PRIVILEGES *) buf;

  if (!OpenProcessToken(GetCurrentProcess(),
			TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    return FALSE;

  if (!LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &tkp->Privileges[0].Luid))
    return FALSE;

  if (!LookupPrivilegeValue(NULL, SE_RESTORE_NAME, &tkp->Privileges[1].Luid))
    return FALSE;

  tkp->PrivilegeCount = 2;
  tkp->Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  tkp->Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;

  return AdjustTokenPrivileges(hToken, FALSE, tkp, sizeof buf, NULL, NULL);
}

// This function examines all paths and filenames on behalf of the backup and
// restore routines. It identifies names containing strings matching the -i or
// -e switches on the command line and returns true if this name is ok to
// process.
bool
ExcludedString(LPCWSTR wczPath)
{
  // This routine does not handle empty strings correctly so just for safe
  // return to skip all empty strings (should not happen normaly).
  if (*wczPath == 0)
    return true;

  if (dwExcludeStrings != 0)
    {
      DWORD dwExcludeString = dwExcludeStrings;
      LPCWSTR wczExcludeString = szExcludeStrings;
      LPCWSTR wczPathEnd = wczPath + wcslen(wczPath);

      do
	{
	  DWORD_PTR dwExclStrLen = wcslen(wczExcludeString);

	  for (LPCWSTR wczPathPtr = wczPath;
	       (DWORD) (wczPathEnd - wczPathPtr) >= dwExclStrLen;
	       wczPathPtr++)
	    if (wcsnicmp(wczPathPtr, wczExcludeString, dwExclStrLen) == 0)
	      return true;

	  wczExcludeString += dwExclStrLen + 1;
	}
      while (--dwExcludeString);
    }

  if (dwIncludeStrings == 0)
    return false;

  // If a directory, do not skip it just because it does not match any of the
  // -i strings.
  switch (wczPath[wcslen(wczPath) - 1])
    {
    case L'\\':
    case L'/':
      return false;
    }

  DWORD dwIncludeString = dwIncludeStrings;
  LPCWSTR wczIncludeString = szIncludeStrings;
  LPCWSTR wczPathEnd = wczPath + wcslen(wczPath);

  do
    {
      DWORD_PTR dwInclStrLen = wcslen(wczIncludeString);

      for (LPCWSTR wczPathPtr = wczPath;
	   (DWORD) (wczPathEnd - wczPathPtr) >= dwInclStrLen; wczPathPtr++)
	if (wcsnicmp(wczPathPtr, wczIncludeString, dwInclStrLen) == 0)
	  return false;

      wczIncludeString += dwInclStrLen + 1;
    }
  while (--dwIncludeString);

  return true;
}

// This function creates the directory specified with the wczPath parameter and
// also creates any directories necessary above it in the tree. It works very
// much like MakeSureDirectoryPathExists() in the image helper library but this
// is a Unicode function.
bool
CreateDirectoryPath(LPCWSTR wczPath)
{
  for (;;)
    {
      if (CreateDirectory(wczPath, NULL))
	return true;

      if (win_errno != ERROR_PATH_NOT_FOUND)
	return false;

      if (wcslen(wczPath) > 32767)
	return false;

      LPWSTR wczNewPath = wcsdup(wczPath);
      if (wczNewPath == NULL)
	return false;

      Sleep(0);

      LPWSTR lpSep;
      for (lpSep = wcschr(wczNewPath, L'/');
	   lpSep != NULL; lpSep = wcschr(lpSep + 1, L'/'))
	*lpSep = L'\\';

      for (lpSep = wcsrchr(wczNewPath, L'\\');
	   lpSep != NULL; lpSep = wcsrchr(wczNewPath, L'\\'))
	{
	  *lpSep = 0;

	  if (CreateDirectory(wczNewPath, NULL))
	    break;
	}

      if (lpSep == NULL)
	if (!CreateDirectory(wczNewPath, NULL))
	  {
	    free(wczNewPath);
	    return false;
	  }

      free(wczNewPath);
    }
}

int
__cdecl
wmain(int argc, LPWSTR *argv)
{
  LPWSTR wczStartDir = NULL;

  if (argc > 1 ? (argv[1][0] | 0x02) != L'/' : true)
    usage();

  EnableBackupPrivileges();

  // Nice argument parse loop :)
  while (argc > 1 ? argv[1][0] ? ((argv[1][0] | 0x02) == L'/') &
	 ~((wcscmp(argv[1], L"-") == 0) | (wcscmp(argv[1], L"--") == 0)) :
	 false : false)
    {
      while ((++argv[1])[0])
	switch (argv[1][0] | 0x20)
	  {
	  case L'c':
	    if (bRestoreMode | bTestMode)
	      usage();
	    bBackupMode = true;
	    break;
	  case L'x':
	    if (bBackupMode | bTestMode)
	      usage();
	    bRestoreMode = true;
	    break;
	  case L't':
	    if (bBackupMode | bRestoreMode)
	      usage();
	    bTestMode = true;
	    break;
	  case L'a':
	    dwArchiveCreation = OPEN_ALWAYS;
	    break;
	  case L'8':
	    bRestoreShortNamesOnly = true;
	    break;
	  case L'o':
	    if (argv[1][1] == L':')
	      {
		if (argv[1][2] == 0)
		  usage();

		for (argv[1] += 1; argv[1][1] != 0; argv[1]++)
		  switch (argv[1][1])
		    {
		    case L'n':
		      bOverwriteOlder = true;
		      break;
		    case L'f':
		      bFreshenExisting = true;
		      bOverwriteOlder = true;
		      break;
		    case L'a':
		      bOverwriteArchived = true;
		      break;
		    default:
		      usage();
		    }
	      }

	    dwExtractCreation = OPEN_ALWAYS;
	    break;
	  case L'l':
	    bListFiles = true;
	    break;
	  case L'v':
	    bVerbose = true;
	    break;
	  case L'j':
	    bLocal = true;
	    break;
	  case L'f':
	    bFilesFromStdInUnicode = argv[1][0] == L'F';
	    bFilesFromStdIn = true;
	    break;
	  case L'r':
	    bBackupRegistrySnapshots = true;
	    break;
	  case L'e':
	    if (dwExcludeStrings)
	      usage();
	    if (argv[1][1] != L':')
	      usage();
	    if (argv[1][2] == 0)
	      usage();
	    szExcludeStrings = wcstok(argv[1] + 2, L",");
	    dwExcludeStrings = 1;
	    while (wcstok(NULL, L","))
	      dwExcludeStrings++;
	    argv[1] += wcslen(argv[1]) - 1;
	    break;
	  case L'i':
	    if (dwIncludeStrings)
	      usage();
	    if (argv[1][1] != L':')
	      usage();
	    if (argv[1][2] == 0)
	      usage();
	    szIncludeStrings = wcstok(argv[1] + 2, L",");
	    dwIncludeStrings = 1;
	    while (wcstok(NULL, L","))
	      dwIncludeStrings++;
	    argv[1] += wcslen(argv[1]) - 1;
	    break;
	  case L'd':
	    if (argv[1][1] != L':')
	      usage();
	    if (argv[1][2] == 0)
	      usage();
	    wczStartDir = argv[1] + 2;
	    argv[1] += wcslen(argv[1]) - 1;
	    break;
	  case L'b':
	    {
	      if (argv[1][1] != L':')
		usage();
	      if (argv[1][2] == 0)
		usage();
	      LPWSTR suffix = NULL;
	      dwBufferSize = wcstoul(argv[1] + 2, &suffix, 0);
	      switch (*suffix)
		{
		case 0:
		  break;
		case L'M':
		  dwBufferSize <<= 10;
		case L'K':
		  dwBufferSize <<= 10;
		  break;
		default:
		  usage();
		}
	      argv[1] += wcslen(argv[1]) - 1;
	      break;
	    }
	  case L's':
	    if (argv[1][1] != L':')
	      usage();
	    if (argv[1][2] == 0)
	      usage();

	    for (argv[1] += 1; argv[1][1] != 0; argv[1]++)
	      {
		switch (argv[1][1])
		  {
		  case L'a':
		    bProcessFileAttribs = false;
		    break;
		  case L'c':
		    bRestoreCompression = false;
		    break;
		  case L'l':
		    bHardLinkSupport = false;
		    break;
		  case L's':
		    bProcessSecurity = FALSE;
		    break;
		  case L't':
		    bProcessFileTimes = false;
		    break;
		  default:
		    usage();
		  }
	      }

	    break;
	  case L'm':
	    if (wcsicmp(argv[1] + 1, L":f") == 0)
	      BackupMethod = BACKUP_METHOD_FULL;
	    else if (wcsicmp(argv[1] + 1, L":d") == 0)
	      BackupMethod = BACKUP_METHOD_DIFF;
	    else if (wcsicmp(argv[1] + 1, L":i") == 0)
	      BackupMethod = BACKUP_METHOD_INC;
	    else
	      usage();

	    argv[1] += 2;
	    break;
	  case L'z':
	    if (argv[1][1] != L':')
	      usage();
	    if (argv[1][2] == 0)
	      usage();
	    wczFilterCmd = argv[1] + 2;
	    argv[1] += wcslen(argv[1]) - 1;
	    break;
	  default:
	    usage();
	  }

      --argc;
      ++argv;
    }

  // A -- switch is just marking end of switch interpreting. Skip over it.
  if (argc > 1 ? wcscmp(argv[1], L"--") == 0 : false)
    {
      ++argv;
      --argc;
    }

  // More than one parameter is only allowed in backup mode.
  if ((argc > 2) & (!bBackupMode))
    usage();

  // Needs one of -c, -x or -t switches.
  if (((int) bBackupMode + (int) bRestoreMode + (int) bTestMode) != 1)
    usage();

  if (bListFiles & (bTestMode | bVerbose))
    {
      fputs("The -l option cannot be used with -t or -v.\r\n", stderr);
      return 1;
    }

  // Are we creating an archive to stdout?
  bool bTargetStdOut =
    argc < 2 ? true : (argv[1][0] == 0) | (wcscmp(argv[1], L"-") == 0);

  if (bListFiles & bBackupMode & bTargetStdOut)
    {
      fputs("Cannot list files when creating an archive to stdout.\r\n",
	    stderr);
      return 1;
    }

  // It needs to be greater than 64 KB.
  if (dwBufferSize < 65536)
    status_exit(XE_BAD_BUFFER);

  // Try to allocate the buffer size (possibly specified on command line).
  Buffer = (LPBYTE) LocalAlloc(LPTR, dwBufferSize);

  if (Buffer == NULL)
    status_exit(XE_NOT_ENOUGH_MEMORY);

  if (bBackupRegistrySnapshots)
    if (!bBackupMode)
      usage();
    else
      CreateRegistrySnapshots();

  WSecurityAttributes sa;
  sa.bInheritHandle = TRUE;
  if (bTargetStdOut)
    hArchive = GetStdHandle(bBackupMode ?
			    STD_OUTPUT_HANDLE : STD_INPUT_HANDLE);
  else
    {
      hArchive = CreateFile(argv[1],
			    bBackupMode ? GENERIC_WRITE : GENERIC_READ,
			    FILE_SHARE_READ | FILE_SHARE_DELETE |
			    (bBackupMode ? 0 : FILE_SHARE_WRITE), &sa,
			    bBackupMode ? dwArchiveCreation : OPEN_EXISTING,
			    (bBackupMode ? FILE_ATTRIBUTE_NORMAL : 0) |
			    FILE_FLAG_SEQUENTIAL_SCAN |
			    FILE_FLAG_BACKUP_SEMANTICS, NULL);

      if (hArchive == INVALID_HANDLE_VALUE)
	status_exit(XE_ARCHIVE_OPEN, argv[1]);
    }

  if (dwArchiveCreation == OPEN_ALWAYS)
    SetFilePointer(hArchive, 0, 0, FILE_END);
  else
    SetEndOfFile(hArchive);

  // If we should filter through a compression utility.
  PROCESS_INFORMATION piFilter = { 0 };
  if (wczFilterCmd != NULL)
    {
      HANDLE hPipe[2];
      if (!CreatePipe(&hPipe[0], &hPipe[1], NULL, 0))
	status_exit(XE_CREATE_PIPE);

      // This is a special trick to make the child process only inherit one end
      // of the pipe.
      WStartupInfo si;
      si.dwFlags = STARTF_USESTDHANDLES;
      if (bBackupMode)
	{
	  SetHandleInformation(hPipe[0],
			       HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

	  si.hStdInput = hPipe[0];
	  si.hStdOutput = hArchive;

	  hArchive = hPipe[1];
	}
      else
	{
	  SetHandleInformation(hPipe[1],
			       HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

	  si.hStdInput = hArchive;
	  si.hStdOutput = hPipe[1];

	  hArchive = hPipe[0];
	}
      si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
      if (!CreateProcess(NULL, wczFilterCmd, NULL, NULL, TRUE, 0, NULL, NULL,
			 &si, &piFilter))
	status_exit(XE_FILTER_EXECUTE, wczFilterCmd);

      CloseHandle(si.hStdInput);
      CloseHandle(si.hStdOutput);
    }

  if (wczStartDir != NULL)
    if (bBackupMode)
      {
	if (!SetCurrentDirectory(wczStartDir))
	  status_exit(XE_CHANGE_DIR, wczStartDir);
      }
    else
      {
	if (!SetCurrentDirectory(wczStartDir))
	  if (CreateDirectoryPath(wczStartDir))
	    {
	      if (!SetCurrentDirectory(wczStartDir))
		status_exit(XE_CHANGE_DIR, wczStartDir);
	    }
	  else
	    status_exit(XE_CREATE_DIR, wczStartDir);
      }
  
  SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

  if (bRestoreMode | bTestMode)
    {
      RestoreDirectoryTree();

      if (bVerbose)
	if (bCancel)
	  fprintf(stderr,
		  "strarc cancelled, %u files %s.\n", dwFileCounter,
		  bTestMode ? "found in archive" : "restored");
	else
	  fprintf(stderr,
		  "strarc done, %u files %s.\n", dwFileCounter,
		  bTestMode ? "found in archive" : "restored");

      return 0;
    }

  if (argc > 2)
    while (argc-- > 2)
      {
	Sleep(0);

	if (bCancel)
	  break;

	wczCurrentPath[(sizeof(wczCurrentPath) / sizeof(*wczCurrentPath)) -
		       14] = 0;

	wcsncpy(wczCurrentPath, argv++[2],
		sizeof(wczCurrentPath) / sizeof(*wczCurrentPath));

	if (wczCurrentPath
	    [(sizeof(wczCurrentPath) / sizeof(*wczCurrentPath)) - 14] != 0)
	  {
	    fprintf(stderr, "strarc: The path is too long: '%ws'\n", argv[1]);
	    continue;
	  }

	size_t iLen = wcslen(wczCurrentPath);
	if (iLen == 0)
	  continue;

	if ((wcscmp(wczCurrentPath, L".") == 0) |
	    (wcscmp(wczCurrentPath, L"..") == 0))
	  {
	    BackupDirectoryTree();
	    continue;
	  }

	for (LPWSTR wczPathSlash = wcschr(wczCurrentPath, L'/');
	     wczPathSlash != NULL;
	     wczPathSlash = wcschr(wczPathSlash + 1, L'/'))
	  *wczPathSlash = L'\\';

	LPWSTR wczFilePart = wcsrchr(wczCurrentPath, L'\\');
	if (wczFilePart == NULL)
	  wczFilePart = wczCurrentPath;
	else
	  wczFilePart++;

	WFileFinder found(wczCurrentPath);
	if (!found)
	  BackupDirectoryTree();
	else
	  do
	    {
	      if ((wcscmp(found.cFileName, L".") == 0) |
		  (wcscmp(found.cFileName, L"..") == 0))
		continue;

	      if (wcslen(found.cFileName) + (wczFilePart - wczCurrentPath) >=
		  (sizeof(wczCurrentPath) / sizeof(*wczCurrentPath)))
		{
		  *wczFilePart = 0;
		  fprintf(stderr, "strarc: The path is too long '%ws%ws'\n",
			  wczCurrentPath, found.cFileName);
		  continue;
		}

	      wcsncpy(wczFilePart, found.cFileName,
		      (sizeof(wczCurrentPath) / sizeof(*wczCurrentPath)) -
		      (wczFilePart - wczCurrentPath));

	      BackupDirectoryTree();
	    }
	  while (found.Next());
      }
  else if (bFilesFromStdIn)
    if (bFilesFromStdInUnicode)
      {
	HANDLE hInputFile = GetStdHandle(STD_INPUT_HANDLE);
	WOverlappedIOC ol;

	if (!ol)
	  status_exit(XE_NOT_ENOUGH_MEMORY);

	for (;;)
	  {
	    Sleep(0);

	    if (bCancel)
	      break;

	    WCHAR wczFile[32768] = L"";

	    if (ol.LineRecvW(hInputFile, wczFile,
			     sizeof(wczFile) / sizeof(*wczFile)) == 0)
	      {
		if (GetLastError() == ERROR_SUCCESS)
		  continue;

		if (GetLastError() != ERROR_HANDLE_EOF)
		  win_perrorA("strarc");

		break;
	      }

	    BackupFile(wczFile, NULL);
	  }
      }
    else
      for (;;)
	{
	  Sleep(0);

	  if (bCancel)
	    break;

	  WCHAR wczFile[32768] = L"";
	  char czFile[32768] = "";

	  if (fgets(czFile, sizeof czFile, stdin) == NULL)
	    {
	      if (ferror(stdin))
		perror("strarc");

	      break;
	    }

	  size_t iLen = strlen(czFile);
	  if (iLen == 0)
	    break;

	  if (iLen < 2)
	    continue;

	  czFile[iLen - 1] = 0;
	  MultiByteToWideChar(CP_ACP, 0, czFile, (int) iLen, wczFile,
			      sizeof(wczFile) / sizeof(*wczFile));

	  BackupFile(wczFile, NULL);
	}
  else
    {
      BackupFile(L".", L"");
      BackupDirectory(wczCurrentPath);
    }

  CloseHandle(hArchive);
  if (piFilter.dwProcessId != 0)
    {
      if (bVerbose)
	fputs("Waiting for filter utility to exit...\r\n", stderr);

      WaitForSingleObject(piFilter.hProcess, INFINITE);

      if (bVerbose)
	{
	  DWORD dwExitCode;
	  if (GetExitCodeProcess(piFilter.hProcess, &dwExitCode))
	    fprintf(stderr, "Filter return value: %i\n", dwExitCode);
	  else
	    win_perrorA("Error getting filter return value");
	}
    }

  if (bVerbose)
    if (bCancel)
      fprintf(stderr,
	      "strarc cancelled, %u files backed up.\n", dwFileCounter);
    else
      fprintf(stderr, "strarc done, %u files backed up.\n", dwFileCounter);

  return 0;
}

// This is enough startup code for this program if compiled to use the DLL CRT.
extern "C" int
wmainCRTStartup()
{
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLine(), &argc);
  if (argv == NULL)
    status_exit(XE_NOT_WINDOWSNT);
  else
    exit(wmain(argc, argv));
}
