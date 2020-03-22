/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2016
*
* strarc.cpp
* Definitions for some common outline members of StrArc class.
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
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NON_CONFORMING_WCSTOK
#define _CRT_NON_CONFORMING_WCSTOK
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
#include <wsync.h>

#include <stdlib.h>

#include "strarc.hpp"

#include "version.h"

#define BOOL_FROM_FLAG(flags, flag) (((flags) & (flag)) != 0)

DWORDLONG
StrArc::GetLibraryVersion()
{
    union
    {
        WORD fields[4];
        DWORDLONG dwl;
    } record = { { STRARC_VERSION_INFO } };

    return record.dwl;
}

void
StrArc::Initialize()
{
    LPBYTE dataptr = (LPBYTE)&FirstMember;
    SSIZE_T datasize = sizeof(*this) - (dataptr - (LPBYTE)this);

    memset(dataptr, 0, datasize);

    FileCounter = 0;
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
    bSkipShortNames = false;
    bNoShortNameWarnings = false;
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

StrArc::~StrArc()
{
    if (szIncludeStrings != NULL)
        free(szIncludeStrings);

    if (szExcludeStrings != NULL)
        free(szExcludeStrings);

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
}

// This function is called on various kinds of non-recoverable errors. It
// prepares an error message and some exception information and calls
// RaiseException().
void
__declspec(noreturn)
StrArc::Exception(XError XE, LPCWSTR Name)
{
    DWORD syscode = GetLastError();
    WErrMsgW syserrmsg;
    bool bDisplaySysErrMsg = false;
    LPCWSTR errmsg = L"";
    NTSTATUS status = (NTSTATUS)XE;

    switch (XE)
    {
    case XE_CANCELLED:
        errmsg = L"\r\nstrarc aborted: Operation cancelled.\r\n";
        status = STATUS_CANCELLED;
        break;
    case XE_CREATE_DIR:
        errmsg = L"\r\nstrarc aborted: Cannot create directory.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_IO_DEVICE_ERROR;
        break;
    case XE_CHANGE_DIR:
        errmsg = L"\r\nstrarc aborted: Cannot change to directory.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_IO_DEVICE_ERROR;
        break;
    case XE_CREATE_FILE:
        errmsg = L"\r\nstrarc aborted: Cannot create file.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_IO_DEVICE_ERROR;
        break;
    case XE_FILE_IO:
        errmsg = L"\r\nstrarc aborted: File I/O error.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_IO_DEVICE_ERROR;
        break;
    case XE_ARCHIVE_IO:
        errmsg = L"\r\nstrarc aborted: Archive I/O error.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_IO_DEVICE_ERROR;
        break;
    case XE_ARCHIVE_OPEN:
        errmsg = L"\r\nstrarc: Archive open error.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_IO_DEVICE_ERROR;
        break;
    case XE_CREATE_PIPE:
        errmsg = L"\r\nstrarc: Cannot create pipe.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_IO_DEVICE_ERROR;
        break;
    case XE_FILTER_EXECUTE:
        errmsg = L"\r\nstrarc: Cannot execute filter.\r\n";
        bDisplaySysErrMsg = true;
        status = RPC_NT_CALL_FAILED;
        break;
    case XE_ARCHIVE_TRUNC:
        errmsg = L"\r\nstrarc aborted: Unexpected end of archive.\r\n";
        status = STATUS_UNEXPECTED_IO_ERROR;
        break;
    case XE_ARCHIVE_BAD_HEADER:
        errmsg = L"\r\nstrarc aborted: Bad archive format, aborted.\r\n";
        status = STATUS_UNEXPECTED_IO_ERROR;
        break;
    case XE_TOO_LONG_PATH:
        errmsg = L"\r\nstrarc aborted: Target path is too long.\r\n";
        status = STATUS_NAME_TOO_LONG;
        break;
    case XE_NOT_ENOUGH_MEMORY:
        errmsg = L"\r\nstrarc aborted: Memory allocation failed.\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_NO_MEMORY;
        break;
    case XE_NOT_ENOUGH_MEMORY_FOR_LINK_TRACKER:
        errmsg =
            L"\r\n"
            L"strarc aborted: Memory allocation for hard link tracking failed.\r\n"
            L"Free some memory or use the -s:l option\r\n";
        bDisplaySysErrMsg = true;
        status = STATUS_NO_MEMORY;
        break;
    case XE_BAD_BUFFER:
        errmsg = L"\r\nstrarc aborted: The stream buffer size is too small.\r\n";
        status = STATUS_INVALID_PARAMETER;
        break;

    case XE_NOERROR:
        errmsg = Name;
        Name = NULL;
        status = STATUS_INVALID_PARAMETER;
        break;

    default:
        errmsg = Name;
        Name = NULL;
        status = XE;
        break;
    }

    ZeroMemory(&ExceptionData, sizeof(ExceptionData));

    if (bDisplaySysErrMsg)
    {
        ExceptionData.SysErrorCode = syscode;
        ExceptionData.SysErrorMessage = syserrmsg;
    }

    ExceptionData.ErrorCode = XE;

    ExceptionData.ErrorMessage = errmsg;

    ExceptionData.ObjectName = Name;

    RaiseException((DWORD)status, EXCEPTION_NONCONTINUABLE, 0, NULL);

    ExitThread(XE);
}

StrArc::XError
StrArc::InitializeBuffer()
{
    // It needs to be greater than 64 KB.
    if (dwBufferSize < 65536)
        return XE_BAD_BUFFER;

    // Try to allocate the buffer size (possibly specified on command line).
    if (!InitializeBuffer(dwBufferSize))
        return XE_NOT_ENOUGH_MEMORY;

    return XE_NOERROR;
}

bool
StrArc::OpenArchive(LPCWSTR wczFilename,
bool bBackupMode,
DWORD dwArchiveCreation)
{
    WSecurityAttributes sa;
    sa.bInheritHandle = TRUE;
    if (wczFilename == NULL)
        hArchive = GetStdHandle(bBackupMode ?
    STD_OUTPUT_HANDLE : STD_INPUT_HANDLE);
    else
    {
        hArchive = CreateFile(wczFilename,
            bBackupMode ? GENERIC_WRITE : GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE |
            (bBackupMode ? 0 : FILE_SHARE_WRITE),
            &sa,
            bBackupMode ?
        dwArchiveCreation : OPEN_EXISTING,
                            (bBackupMode ? FILE_ATTRIBUTE_NORMAL : 0) |
                            FILE_FLAG_SEQUENTIAL_SCAN |
                            FILE_FLAG_BACKUP_SEMANTICS,
                            NULL);

        if (hArchive == INVALID_HANDLE_VALUE)
            return false;
    }

    if (dwArchiveCreation == OPEN_ALWAYS)
        SetFilePointer(hArchive, 0, 0, FILE_END);
    else
        SetEndOfFile(hArchive);

    return true;
}

bool
StrArc::OpenFilterUtility(LPWSTR wczFilterCmd,
bool bBackupMode)
{
    HANDLE hPipe[2];
    if (!CreatePipe(&hPipe[0], &hPipe[1], NULL, 0))
        return false;

    // This makes child process only inherit one end of the pipe.
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
    if (!CreateProcess(NULL, wczFilterCmd, NULL, NULL, TRUE, 0, NULL,
        NULL, &si, &piFilter))
        return false;

    CloseHandle(si.hStdInput);
    CloseHandle(si.hStdOutput);

    return true;
}

bool
StrArc::OpenWorkingDirectory(LPCWSTR wczStartDir, bool bBackupMode)
{
    HANDLE root_dir;
    UNICODE_STRING start_dir;
    if (wczStartDir == NULL)
    {
        root_dir = NtCurrentDirectoryHandle();
        RtlCreateUnicodeString(&start_dir, L"");
    }
    else
    {
        root_dir = NULL;

        NTSTATUS status =
            RtlDosPathNameToNtPathName_U(wczStartDir, &start_dir, NULL, NULL);

        if (!NT_SUCCESS(status))
        {
            SetLastError(RtlNtStatusToDosError(status));
            return false;
        }

        if (bVerbose)
            oem_printf(stderr,
            "Working directory is '%1!wZ!'%%n",
            &start_dir);
    }

    if (bBackupMode)
    {
        RootDirectory =
            NativeOpenFile(root_dir,
                &start_dir,
                FILE_LIST_DIRECTORY |
                FILE_TRAVERSE,
                OBJ_CASE_INSENSITIVE,
                FILE_SHARE_READ |
                FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
                FILE_DIRECTORY_FILE |
                FILE_OPEN_FOR_BACKUP_INTENT);
    }
    else
    {
        RootDirectory =
            NativeCreateDirectory(root_dir,
                &start_dir,
                FILE_LIST_DIRECTORY |
                FILE_TRAVERSE,
                OBJ_CASE_INSENSITIVE,
                NULL,
                FILE_ATTRIBUTE_NORMAL,
                FILE_SHARE_READ |
                FILE_SHARE_WRITE |
                FILE_SHARE_DELETE,
                FILE_DIRECTORY_FILE |
                FILE_OPEN_FOR_BACKUP_INTENT,
                TRUE);
    }

    DWORD dwLastError = GetLastError();

    RtlFreeUnicodeString(&start_dir);

    if (RootDirectory == INVALID_HANDLE_VALUE)
    {
        SetLastError(dwLastError);
        return false;
    }

    return true;
}

StrArc::StrArc()
{
    Initialize();
}

StrArc::StrArc(STRARC_OPERATION_MODE OperationMode,
    STRARC_OPERATIONAL_FLAGS OperationalFlags,
    BackupMethods BackupMethod,
    LPCWSTR wczArchiveFile,
    bool bAppendToArchive,
    DWORD dwBufferSize,
    LPWSTR wczFilterCmd,
    LPCWSTR wczStartDir)
{
    Initialize();

    bool bBackupMode = false;

    switch (OperationMode)
    {
    case STRARC_BACKUP_MODE:
        bBackupMode = true;
        break;

    case STRARC_RESTORE_MODE:
        break;

    case STRARC_TEST_MODE:
        bTestMode = true;
        break;

    default:
        Exception(XE_NOERROR, L"Invalid operation mode.");
    }

    DWORD dwArchiveCreation = CREATE_ALWAYS;
    if (bAppendToArchive)
        dwArchiveCreation = OPEN_ALWAYS;

    this->BackupMethod = BackupMethod;

    this->dwBufferSize = dwBufferSize;

    this->bBackupRegistrySnapshots =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_BACKUP_REGISTRY_SNAPSHOTS);

    this->bVerbose =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_VERBOSE);

    this->bListFiles =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_LIST_FILES);

    this->bListOnly =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_LIST_ONLY);

    this->bLocal =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_FOLLOW_REPARSE);

    this->bProcessSecurity =
        !BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_PROCESS_SECURITY);

    this->bProcessFileTimes =
        !BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_PROCESS_FILE_TIME);

    this->bProcessFileAttribs =
        !BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_PROCESS_FILE_ATTRIBUTES);

    this->bHardLinkSupport =
        !BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_TRACK_HARD_LINKS);

    this->bRestoreCompression =
        !BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_RESTORE_COMPRESSION);

    this->bSkipShortNames =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_SHORT_NAMES);

    this->bNoShortNameWarnings =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_NO_SHORT_NAME_WARNINGS);

    this->bOverwriteOlder =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_OVERWRITE_OLDER);

    this->bOverwriteArchived =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_OVERWRITE_ARCHIVED);

    this->bFreshenExisting =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_FRESHEN_EXISTING);

    this->bRestoreShortNamesOnly =
        BOOL_FROM_FLAG(OperationalFlags, STRARC_FLAG_RESTORE_SHORT_NAMES_ONLY);

    XError bufferstatus = InitializeBuffer();

    if (bufferstatus != XE_NOERROR)
        Exception(bufferstatus);

    if (bBackupRegistrySnapshots)
        if (!bBackupMode)
            Exception(XE_NOERROR,
            L"Registry snaphot feature only available for "
            L"backup operation.");
        else
            CreateRegistrySnapshots();

    if (!bListOnly)
        if (!OpenArchive(wczArchiveFile,
            bBackupMode,
            dwArchiveCreation))
            Exception(XE_ARCHIVE_OPEN, wczArchiveFile);

    // If we should filter through a compression utility.
    if (wczFilterCmd != NULL)
        if (!OpenFilterUtility(wczFilterCmd, bBackupMode))
            Exception(XE_FILTER_EXECUTE, wczFilterCmd);

    if (!OpenWorkingDirectory(wczStartDir, bBackupMode))
        Exception(XE_CHANGE_DIR, wczStartDir);

    if (bBackupMode && (dwBufferSize < HEADER_SIZE))
        Exception(XE_BAD_BUFFER);
}

void
StrArc::BackupFilenamesFromStreamW(HANDLE hInputFile)
{
    WOverlappedIOC ol;

    if (!ol)
        Exception(XE_NOT_ENOUGH_MEMORY);

    for (;;)
    {
        YieldSingleProcessor();

        if (bCancel)
            break;

        FullPath.Length = (USHORT)
            ol.LineRecvW(hInputFile,
            FullPath.Buffer,
            FullPath.MaximumLength >> 1);

        if (FullPath.Length == 0)
        {
            if (GetLastError() == NO_ERROR)
                continue;

            if (GetLastError() != ERROR_HANDLE_EOF)
                win_perrorA("strarc");

            break;
        }

        BackupFile(&FullPath, NULL, false);
    }
}

void
StrArc::BackupFilenamesFromStreamA(HANDLE hInputFile)
{
    WOverlappedIOC ol;

    if (!ol)
        (XE_NOT_ENOUGH_MEMORY);

    for (;;)
    {
        YieldSingleProcessor();

        if (bCancel)
            break;

        WHeapMem<char> czFile(32768,
            HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY);

        size_t iLen =
            ol.LineRecv(hInputFile,
            czFile,
            (DWORD)czFile.Count());

        if (iLen == 0)
        {
            if (GetLastError() == NO_ERROR)
                continue;

            if (GetLastError() != ERROR_HANDLE_EOF)
                win_perrorA("strarc");

            break;
        }

        ANSI_STRING file;
        RtlInitAnsiString(&file, czFile);

        NTSTATUS status =
            RtlAnsiStringToUnicodeString(&FullPath,
            &file,
            FALSE);

        if (!NT_SUCCESS(status))
        {
            WErrMsgA errmsg(RtlNtStatusToDosError(status));
            fprintf(stderr,
                "strarc: Too long path: '%s'\n", (LPSTR)czFile);
            continue;
        }

        BackupFile(&FullPath, NULL, false);
    }
}

void
StrArc::SetIncludeExcludeStrings(LPDWORD dwStrings,
LPWSTR *szStrings,
LPCWSTR wczNewStrings)
{
    if (*szStrings != NULL)
        free(*szStrings);

    if (wczNewStrings == NULL)
    {
        *dwStrings = 0;
        *szStrings = NULL;
        return;
    }

    LPWSTR str = _wcsdup(wczNewStrings);
    if (str == NULL)
        Exception(XE_NOT_ENOUGH_MEMORY);

    *szStrings = wcstok(str, L",");
    *dwStrings = 1;
    while (wcstok(NULL, L","))
        ++ *dwStrings;
}
