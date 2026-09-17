/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2022
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

#define WIN32_NO_STATUS
#include <windows.h>
#include <intsafe.h>
#undef WIN32_NO_STATUS
#include <ntdll.h>

#include <winstrct.h>
#include <wio.h>

#include "strarc.hpp"

// This function backs up an object (file or directory) to the archive stream.

bool
StrArc::ReadFileStreamsToArchive(PUNICODE_STRING File,
HANDLE hFile)
{
    bBackupFailed = true;
    if (BackupPrefix == NULL)
    {
        BackupPrefix = (LPBYTE)LocalAlloc(LMEM_FIXED, HEADER_SIZE + USHORT_MAX);
        if (BackupPrefix == NULL)
            Exception(XE_NOT_ENOUGH_MEMORY);
    }

    LPVOID lpCtx = NULL;
    LONGLONG remaining = 0;
    DWORD error = NO_ERROR;
    DWORD prefix = 0;
    DWORD prefixSize = HEADER_SIZE;

    for (;;)
    {
        YieldSingleProcessor();
        if (bCancel)
        {
            BackupRead(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);
            SetLastError(ERROR_OPERATION_ABORTED);
            return false;
        }

        // BackupRead requires a buffer larger than WIN32_STREAM_ID. Keep bulk
        // reads, then parse every stream boundary within the returned bytes.
        DWORD count = 0;
        if (!BackupRead(hFile, Buffer, dwBufferSize, &count, FALSE,
            bProcessSecurity, &lpCtx))
        {
            error = GetLastError();
            goto failed;
        }
        if (count == 0)
        {
            if ((remaining != 0) || (prefix != 0))
            {
                error = ERROR_HANDLE_EOF;
                goto failed;
            }

            BackupRead(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);
            bBackupFailed = false;
            ++FileCounter;
            if (bVerbose)
                fputs(", EOF\r\n", stderr);
            return true;
        }

        DWORD offset = 0;
        while (offset < count)
        {
            DWORD size = count - offset;
            if (remaining > 0)
            {
                if (remaining < size)
                    size = (DWORD)remaining;
                WriteArchive(Buffer + offset, size);
                remaining -= size;
            }
            else
            {
                if (size > prefixSize - prefix)
                    size = prefixSize - prefix;
                CopyMemory(BackupPrefix + prefix, Buffer + offset, size);
                prefix += size;

                LPWIN32_STREAM_ID stream = (LPWIN32_STREAM_ID)BackupPrefix;
                if ((prefix == HEADER_SIZE) && (prefixSize == HEADER_SIZE))
                {
                    // Bound the prefix and extractor arithmetic. Unknown
                    // nonzero stream IDs remain supported.
                    if ((stream->dwStreamId == BACKUP_INVALID) ||
                        (stream->Size.QuadPart < 0) ||
                        (stream->dwStreamNameSize > USHORT_MAX) ||
                        (stream->dwStreamNameSize & 1) ||
                        (stream->dwStreamNameSize > dwBufferSize - HEADER_SIZE) ||
                        (stream->Size.QuadPart >
                            MAXLONGLONG - HEADER_SIZE - stream->dwStreamNameSize))
                    {
                        error = ERROR_INVALID_DATA;
                        goto failed;
                    }
                    prefixSize += stream->dwStreamNameSize;
                }

                // Hold an incomplete header/name entirely outside the archive.
                if (prefix == prefixSize)
                {
                    remaining = stream->Size.QuadPart;
                    WriteArchive(BackupPrefix, prefixSize);
                    prefix = 0;
                    prefixSize = HEADER_SIZE;
                }
            }
            offset += size;
        }
    }

failed:
    // Do not trust output bytes from a failed BackupRead call. Only bytes
    // already written to the archive reduce the padding still required.
    WErrMsgA errmsg(error);
    oem_printf(stderr, "strarc: Cannot read '%1!wZ!': %2%%n"
        "strarc: Marking this entry as failed and continuing.%%n", File, errmsg);
    ++FailedFileCounter;
    BackupRead(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);

    memset(Buffer, 0, dwBufferSize);
    while (remaining > 0)
    {
        YieldSingleProcessor();
        if (bCancel)
        {
            SetLastError(ERROR_OPERATION_ABORTED);
            return false;
        }

        DWORD count = remaining > dwBufferSize ? dwBufferSize : (DWORD)remaining;
        WriteArchive(Buffer, count);
        remaining -= count;
    }

    if (bCancel)
    {
        SetLastError(ERROR_OPERATION_ABORTED);
        return false;
    }

    header->dwStreamId = BACKUP_INVALID;
    header->dwStreamAttributes = STRARC_FAILED_FILE;
    header->Size.QuadPart = sizeof(error);
    header->dwStreamNameSize = 0;
    CopyMemory(Buffer + HEADER_SIZE, &error, sizeof(error));
    WriteArchive(Buffer, HEADER_SIZE + sizeof(error));
    bBackupFailed = false;
    // The pipe-based copy helper also uses this method and reads GetLastError.
    SetLastError(error);
    return false;
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
    // A previous cancellation or output failure may have left an unfinished
    // entry. Source errors with a complete failure record allow continuation.
    if (bBackupFailed || bCancel)
        return false;

    // This could be a directory. In that case, we do not want to skip it just
    // because it does not match any of the -i strings, but still skip if it
    // matches any of the -e strings. This is to find files and directories in
    // this directory that may match an -i string.

    ACCESS_MASK file_access = 0;

    if (bListOnly)
        file_access |= FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    else
        file_access |= GENERIC_READ;

    if ((BackupMethod == BACKUP_METHOD_FULL) ||
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

    if ((!bLocal) && (hFile == INVALID_HANDLE_VALUE) &&
        (GetLastError() == ERROR_INVALID_PARAMETER))
    {
        hFile =
            NativeOpenFile(RootDirectory,
                File,
                file_access,
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                FILE_OPEN_FOR_BACKUP_INTENT |
                FILE_SEQUENTIAL_ONLY);
    }

    if (hFile == INVALID_HANDLE_VALUE)
    {
        WErrMsgA errmsg;
        oem_printf(stderr,
            "strarc: Cannot open '%1!wZ!': %2%%n",
            File, errmsg);
        return false;
    }

    BY_HANDLE_FILE_INFORMATION file_info;
    if (!GetFileInformationByHandle(hFile, &file_info))
    {
        WErrMsgA errmsg;
        oem_printf(stderr,
            "strarc: Cannot get file information for '%1!wZ!': %2%%n",
            File, errmsg);
        NtClose(hFile);
        return false;
    }

    bool bExcludeThis;
    bool bIncludeThis;
    ExcludedString(File, &file_info, &bExcludeThis, &bIncludeThis);
    if (bExcludeThis)
    {
        NtClose(hFile);
        return true;
    }

    if (bTraverseDirectories &&
        (file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(bLocal &&
        (file_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)))
    {
        if (bListOnly)
        {
            NtClose(hFile);

            hFile =
                NativeOpenFile(RootDirectory,
                    File,
                    GENERIC_READ,
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    FILE_OPEN_FOR_BACKUP_INTENT |
                    FILE_SEQUENTIAL_ONLY |
                    (bLocal ? FILE_OPEN_REPARSE_POINT : 0));

            if ((!bLocal) && (hFile == INVALID_HANDLE_VALUE) &&
                (GetLastError() == ERROR_INVALID_PARAMETER))
            {
                hFile =
                    NativeOpenFile(RootDirectory,
                        File,
                        GENERIC_READ,
                        0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_OPEN_FOR_BACKUP_INTENT |
                        FILE_SEQUENTIAL_ONLY);
            }

            if (hFile == INVALID_HANDLE_VALUE)
            {
                WErrMsgA errmsg;
                oem_printf(stderr,
                    "strarc: Cannot open '%1!wZ!': %2%%n",
                    File, errmsg);
                return false;
            }
        }

        BackupDirectory(File, hFile);
    }

    if (bBackupFailed || bCancel)
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
            "%1!wZ!",
            File);

        fprintf(stderr,
            ", attr=%s (%#x)",
            GetFileAttributesDescription(file_info.dwFileAttributes),
            file_info.dwFileAttributes);
    }

    if ((BackupMethod == BACKUP_METHOD_DIFF ||
        BackupMethod == BACKUP_METHOD_INC) &&
        (file_info.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) == 0)
    {
        if (bVerbose)
            fprintf(stderr, ", not changed.\r\n");

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

        ++FileCounter;
        NtClose(hFile);
        return true;
    }

    // Check if this is a registry snapshot file.
    if (bBackupRegistrySnapshots &&
        File->Length >= sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION) &&
        (wcscmp(File->Buffer + (File->Length -
            sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION)) /
            sizeof(*REGISTRY_SNAPSHOT_FILE_EXTENSION) + 1,
            REGISTRY_SNAPSHOT_FILE_EXTENSION) == 0))
    {
        NativeDeleteFile(hFile);

        File->Length -= sizeof(REGISTRY_SNAPSHOT_FILE_EXTENSION) - 2;
    }

    header->dwStreamId = BACKUP_INVALID;
    header->dwStreamAttributes = STRARC_MAGIC;
    header->Size.QuadPart = sizeof BY_HANDLE_FILE_INFORMATION;
    header->dwStreamNameSize = File->Length;
    memcpy(header->cStreamName, File->Buffer, header->dwStreamNameSize);
    *(PBY_HANDLE_FILE_INFORMATION)
        (Buffer + HEADER_SIZE + header->dwStreamNameSize) = file_info;

    // Save without archive attribute in archive, unless COPY mode.
    if (BackupMethod != BACKUP_METHOD_COPY)
        ((PBY_HANDLE_FILE_INFORMATION)
        (Buffer + HEADER_SIZE + header->dwStreamNameSize))->
        dwFileAttributes &= ~FILE_ATTRIBUTE_ARCHIVE;

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
            if (finddata.FindFirst(hParentDir, &file_part) &&
                (finddata.Base.ShortNameLength > 0))
            {
                header->Size.QuadPart += 26;

                LPSTR shortname = (LPSTR)
                    (Buffer + HEADER_SIZE + header->dwStreamNameSize +
                        sizeof BY_HANDLE_FILE_INFORMATION);

                memset(shortname, 0, 26);
                memcpy(shortname,
                    finddata.Base.ShortName,
                    finddata.Base.ShortNameLength);

                if (bVerbose)
                    oem_printf(stderr,
                        ", short='%1!.*ws!'",
                        finddata.Base.ShortNameLength >> 1,
                        finddata.Base.ShortName);
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
            ", short='%1!wZ!'",
            ShortName);
    }

    if (bVerbose)
        fprintf(stderr, ", header: %u bytes",
        HEADER_SIZE + header->dwStreamNameSize + header->Size.LowPart);

    bBackupFailed = true;
    WriteArchive(Buffer, HEADER_SIZE + header->dwStreamNameSize +
        header->Size.LowPart);

    PUNICODE_STRING LinkName = NULL;
    if ((file_info.nNumberOfLinks > 1) && (bHardLinkSupport))
    {
        LARGE_INTEGER FileIndex = { 0 };
        FileIndex.LowPart = file_info.nFileIndexLow;
        FileIndex.HighPart = file_info.nFileIndexHigh;

        LinkName =
            MatchLink(file_info.dwVolumeSerialNumber,
            FileIndex.QuadPart,
            File, false);
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
            ", link to '%1!wZ!': %2!u! bytes%%n",
            LinkName, HEADER_SIZE + header->Size.LowPart);

        WriteArchive((LPBYTE)LinkName->Buffer, LinkName->Length);

        bBackupFailed = false;
        ++FileCounter;
        return true;
    }

    bool bResult = ReadFileStreamsToArchive(File, hFile);

    // Publish a hard-link source only after its entire backup succeeded.
    if (bResult && (file_info.nNumberOfLinks > 1) && bHardLinkSupport)
    {
        LARGE_INTEGER FileIndex = { 0 };
        FileIndex.LowPart = file_info.nFileIndexLow;
        FileIndex.HighPart = file_info.nFileIndexHigh;
        MatchLink(file_info.dwVolumeSerialNumber, FileIndex.QuadPart, File);
    }

    if (bResult &&
        (BackupMethod == BACKUP_METHOD_FULL ||
            BackupMethod == BACKUP_METHOD_INC) &&
            (file_info.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0 &&
        !NativeSetFileAttributes(hFile,
            file_info.dwFileAttributes &
            ~FILE_ATTRIBUTE_ARCHIVE))
    {
        WErrMsgA errmsg;
        oem_printf(stderr,
            "strarc: Error resetting archive attribute on "
            "%1!wZ!: %2%%n",
            File, errmsg);
    }

    NtClose(hFile);

    return bResult;
}

void
StrArc::BackupDirectory(PUNICODE_STRING Path, HANDLE Handle)
{
    UNICODE_STRING base_path = *Path;

    if ((base_path.Length == 2) &&
        (base_path.Buffer[0] == L'.'))
        base_path.Length = 0;
    else if (base_path.Length > 0)
    {
        NTSTATUS status = RtlAppendUnicodeToString(&base_path, L"\\");

        if (!NT_SUCCESS(status))
        {
            oem_printf(stderr,
                "strarc: Path is too long: '%1!wZ!'%%n",
                Path);
            return;
        }
    }

    BYTE short_name_buffer[sizeof(finddata.Base.ShortName)] = { 0 };
    UNICODE_STRING short_name = { 0 };
    short_name.MaximumLength = sizeof(short_name_buffer);
    short_name.Buffer = (PWSTR)short_name_buffer;

    for (finddata.FindFirst(Handle);
        ;
        finddata.FindNext(Handle))
    {
        YieldSingleProcessor();

        if (!finddata)
        {
            DWORD dwLastError = finddata.GetLastWin32Error();
            if (dwLastError == ERROR_NO_MORE_FILES)
                break;
            else
            {
                WErrMsgA errmsg(dwLastError);
                oem_printf(stderr,
                    "strarc: Error reading directory '%1!wZ!': %2%%n",
                    Path, errmsg);

                return;
            }
        }

        if ((finddata.Base.FileNameLength == 2 &&
            finddata.FileName[0] == L'.') ||
            (finddata.Base.FileNameLength == 4 &&
                finddata.FileName[0] == L'.' &&
                finddata.FileName[1] == L'.'))
        {
            continue;
        }

        if (bCancel)
            return;

        if (finddata.Base.FileNameLength > USHORT_MAX)
        {
            oem_printf(stderr,
                "strarc: Path is too long: %1!wZ!",
                Path);

            oem_printf(stderr,
                "\\%1!.*ws!%%n",
                finddata.Base.FileNameLength >> 1, finddata.FileName);

            continue;
        }

        UNICODE_STRING entry_name;
        InitCountedUnicodeString(&entry_name,
            finddata.FileName,
            (USHORT)finddata.Base.FileNameLength);

        UNICODE_STRING name = base_path;

        NTSTATUS status =
            RtlAppendUnicodeStringToString(&name,
            &entry_name);

        if (!NT_SUCCESS(status))
        {
            oem_printf(stderr,
                "strarc: Path is too long: %1!wZ!",
                Path);

            oem_printf(stderr,
                "\\%1!.*ws!%%n",
                finddata.Base.FileNameLength >> 1, finddata.FileName);

            continue;
        }

        if (bSkipShortNames)
        {
            short_name.Length = 0;
        }
        else
        {
            short_name.Length = finddata.Base.ShortNameLength;
            if (finddata.Base.ShortNameLength > 0)
            {
                memcpy(short_name.Buffer,
                    finddata.Base.ShortName,
                    finddata.Base.ShortNameLength);
            }
        }

        if (!BackupFile(&name, &short_name, true) &&
            (bBackupFailed || bCancel))
            return;
    }
}
