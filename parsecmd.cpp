/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2022
*
* parsecmd.cpp
* This contains definition for command line parse functions.
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
#define WIN32_NO_STATUS
#include <windows.h>
#include <intsafe.h>
#include <shellapi.h>

#undef WIN32_NO_STATUS
#include <ntdll.h>
#include <winstrct.h>
#include <wio.h>
#include <wntsecur.h>
#include <wprocess.h>

#include <stdlib.h>

#include "strarc.hpp"
#include "version.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ntdll.lib")

int
usage()
{
    fprintf(stderr,
        "Backup Stream archive I/O Utility, version " STRARC_VERSION "\r\n"
        "Build date: " __DATE__
        ", Copyright (C) Olof Lagerkvist 2004-2022\r\n"
        "http://www.ltr-data.se      olof@ltr-data.se\r\n"
        "\n"
        "Usage:\r\n"
        "\n"
        "strarc -c[afjr] [-z:CMD] [-m:f|d|i] [-l|v] [-s:ls8] [-b:SIZE]\r\n"
        "       [-e:EXCLUDE[,...]] [-i:INCLUDE[,...]] [-d:DIR] [ARCHIVE|-n] [LIST ...]\r\n"
        "\n"
        "strarc -x [-8] [-z:CMD] [-l|v] [-s:aclst8] [-o[:afn]] [-b:SIZE] [-w:8]\r\n"
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
        "-n     No actual backup operation. Used for example with -l to list files that\r\n"
        "       would have been backed up.\r\n"
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
        "       ignored for directories.\r\n"
        "\n"
        "-w:8   Do not display any warnings when short 8.3 names cannot be restored.\r\n"
        "\n"
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
        "       8 - Skip storing short 8.3 names in archive on backup, or skip restoring\n"
        "           such names on restore.\r\n"
        "\n"
        "-- General options --\r\n" "\n"
        "-e     Exclude paths and files where any part of the relative path matches any\r\n"
        "       string in specified comma-separated list.\r\n"
        "\n"
        "-i     Include only paths and files where any part of the relative path matches\r\n"
        "       any string in specified comma-separated list.\r\n"
        "       Default is to include all files and directories. -e takes presedence\r\n"
        "       over -i.\r\n"
        "\n"
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
        "version should be available.\r\n",
        TO_h(DEFAULT_STREAM_BUFFER_SIZE),
        TO_p(DEFAULT_STREAM_BUFFER_SIZE));

    return 1;
}

int
StrArc::Main(int argc, LPWSTR *argv)
{
    if (argc > 1 ? (argv[1][0] | 0x02) != L'/' : true)
        return usage();

    bool bBackupMode = false;
    bool bRestoreMode = false;
    bool bFilesFromStdIn = false;
    bool bFilesFromStdInUnicode = false;
    DWORD dwArchiveCreation = CREATE_ALWAYS;
    LPWSTR wczFilterCmd = NULL;
    LPWSTR wczStartDir = NULL;

    // Nice argument parse loop :)
    while (argc > 1 ? argv[1][0] ? ((argv[1][0] | 0x02) == L'/') &
        ~((wcscmp(argv[1], L"-") == 0) | (wcscmp(argv[1], L"--") == 0)) :
        false : false)
    {
        while ((++argv[1])[0])
            switch (argv[1][0] | 0x20)
            {
            case L'c':
                if (bRestoreMode || bTestMode)
                    return usage();
                bBackupMode = true;
                break;
            case L'x':
                if (bBackupMode || bTestMode)
                    return usage();
                bRestoreMode = true;
                break;
            case L't':
                if (bBackupMode || bRestoreMode)
                    return usage();
                bTestMode = true;
                break;
            case L'a':
                dwArchiveCreation = OPEN_ALWAYS;
                break;
            case L'8':
                bRestoreShortNamesOnly = true;
                break;
            case L'n':
                if (!bBackupMode)
                    return usage();
                bListOnly = true;
                break;
            case L'o':
                if (argv[1][1] == L':')
                {
                    if (argv[1][2] == 0)
                        return usage();

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
                            return usage();
                        }
                }

                dwExtractCreation = FILE_OPEN_IF;
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
                    return usage();
                if (argv[1][1] != L':')
                    return usage();
                if (argv[1][2] == 0)
                    return usage();
                SetExcludeStrings(argv[1] + 2);
                argv[1] += wcslen(argv[1]) - 1;
                break;
            case L'i':
                if (dwIncludeStrings)
                    return usage();
                if (argv[1][1] != L':')
                    return usage();
                if (argv[1][2] == 0)
                    return usage();
                SetIncludeStrings(argv[1] + 2);
                argv[1] += wcslen(argv[1]) - 1;
                break;
            case L'd':
                if (argv[1][1] != L':')
                    return usage();
                if (argv[1][2] == 0)
                    return usage();
                wczStartDir = argv[1] + 2;
                argv[1] += wcslen(argv[1]) - 1;
                break;
            case L'b':
            {
                if (argv[1][1] != L':')
                    return usage();
                if (argv[1][2] == 0)
                    return usage();
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
                    return usage();
                }
                argv[1] += wcslen(argv[1]) - 1;
                break;
            }
            case L's':
                if (argv[1][1] != L':')
                    return usage();
                if (argv[1][2] == 0)
                    return usage();

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
                    case L'8':
                        bSkipShortNames = true;
                        break;
                    default:
                        return usage();
                    }
                }

                break;
            case L'w':
                if (argv[1][1] != L':')
                    return usage();
                if (argv[1][2] == 0)
                    return usage();

                for (argv[1] += 1; argv[1][1] != 0; argv[1]++)
                {
                    switch (argv[1][1])
                    {
                    case L'8':
                        if (!bRestoreMode)
                            return usage();

                        bNoShortNameWarnings = true;
                        break;
                    default:
                        return usage();
                    }
                }

                break;
            case L'm':
                if (_wcsicmp(argv[1] + 1, L":f") == 0)
                    BackupMethod = BACKUP_METHOD_FULL;
                else if (_wcsicmp(argv[1] + 1, L":d") == 0)
                    BackupMethod = BACKUP_METHOD_DIFF;
                else if (_wcsicmp(argv[1] + 1, L":i") == 0)
                    BackupMethod = BACKUP_METHOD_INC;
                else
                    return usage();

                argv[1] += 2;
                break;
            case L'z':
                if (argv[1][1] != L':')
                    return usage();
                if (argv[1][2] == 0)
                    return usage();
                wczFilterCmd = argv[1] + 2;
                argv[1] += wcslen(argv[1]) - 1;
                break;
            default:
                return usage();
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
    if ((argc > 2) && (!bBackupMode))
        return usage();

    // Needs one of -c, -x or -t switches.
    if (((int)bBackupMode + (int)bRestoreMode + (int)bTestMode) != 1)
        return usage();

    if (bListFiles && (bTestMode || bVerbose))
    {
        fputs("The -l option cannot be used with -t or -v.\r\n", stderr);
        return 1;
    }

    // Are we creating an archive to stdout?
    bool bTargetStdOut = (!bListOnly) &&
        (argc < 2 || (argv[1][0] == 0) || (wcscmp(argv[1], L"-") == 0));

    if (bListFiles && bBackupMode && bTargetStdOut)
    {
        fputs("Cannot list files when creating an archive to stdout.\r\n",
            stderr);
        return 1;
    }

    XError bufferstatus = InitializeBuffer();

    if (bufferstatus != XE_NOERROR)
        Exception(bufferstatus);

    if (bBackupRegistrySnapshots)
        if (!bBackupMode)
            return usage();
        else
            if (!CreateRegistrySnapshots())
            {
                WErrMsgA errmsg;
                oem_printf(stderr,
                    "Error getting registry hive list: %1%%n"
                    "No registry snapshots will be backed up.%%n", errmsg);
            }

    if (!bListOnly && !OpenArchive(bTargetStdOut ? NULL : argv[1],
        bBackupMode,
        dwArchiveCreation))
    {
        Exception(XE_ARCHIVE_OPEN, bTargetStdOut ? NULL : argv[1]);
    }

    // If we should filter through a compression utility.
    if (wczFilterCmd != NULL && !OpenFilterUtility(wczFilterCmd, bBackupMode))
    {
        Exception(XE_FILTER_EXECUTE, wczFilterCmd);
    }

    argv++;
    argc--;

    if (!OpenWorkingDirectory(wczStartDir, bBackupMode))
    {
        Exception(XE_CHANGE_DIR, wczStartDir);
    }

    if (bRestoreMode || bTestMode)
    {
        RestoreDirectoryTree();

        if (bVerbose)
            if (bCancel)
                fprintf(stderr,
                    "strarc cancelled, %I64u file%s %s.\n",
                    FileCounter,
                    FileCounter != 1 ? "s" : "",
                    bTestMode ? "found in archive" : "restored");
            else
                fprintf(stderr,
                    "strarc done, %I64u file%s %s.\n",
                    FileCounter,
                    FileCounter != 1 ? "s" : "",
                    bTestMode ? "found in archive" : "restored");

        return 0;
    }

    if (dwBufferSize < HEADER_SIZE)
        Exception(XE_BAD_BUFFER);

    if (argc > 1)
        BackupFiles(argc, argv);
    else if (bFilesFromStdIn)
        if (bFilesFromStdInUnicode)
            BackupFilenamesFromStreamW(GetStdHandle(STD_INPUT_HANDLE));
        else
            BackupFilenamesFromStreamA(GetStdHandle(STD_INPUT_HANDLE));
    else
        BackupCurrentDirectory();

    if (bVerbose)
        if (bCancel)
            fprintf(stderr,
                "strarc cancelled, %I64u file%s %s.\n",
                FileCounter,
                FileCounter != 1 ? "s" : "",
                bListOnly ? "found" : "backed up");
        else
            fprintf(stderr,
                "strarc done, %I64u file%s %s.\n",
                FileCounter,
                FileCounter != 1 ? "s" : "",
                bListOnly ? "found" : "backed up");

    return 0;
}
