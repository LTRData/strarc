/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2022
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

#define WIN32_NO_STATUS
#include <windows.h>
#include <intsafe.h>

#undef WIN32_NO_STATUS
#include <ntdll.h>
#include <winstrct.h>
#include <winioctl.h>
#include <wio.h>

#include "strarc.hpp"
#include "lnk.h"

void
StrArc::FillEntireBuffer(DWORD &dwBytesRead,
    PLARGE_INTEGER BytesToRead)
{
    if (bVerbose)
        fprintf(stderr, ", [id=%s, attr=%s, size=0x%.8x%.8x",
            GetStreamIdDescription(header->dwStreamId),
            GetStreamAttributesDescription(header->dwStreamAttributes),
            header->Size.HighPart, header->Size.LowPart);

    BytesToRead->QuadPart =
        HEADER_SIZE + header->dwStreamNameSize + header->Size.QuadPart -
        dwBytesRead;

    if (BytesToRead->QuadPart > 0)
    {
        DWORD FirstBlockDataToRead =
            (BytesToRead->QuadPart > (LONGLONG)(dwBufferSize - dwBytesRead)) ?
            dwBufferSize - dwBytesRead : BytesToRead->LowPart;

        if (FirstBlockDataToRead > 0)
        {
            DWORD FirstBlockDataDone =
                ReadArchive(Buffer + dwBytesRead,
                    FirstBlockDataToRead);

            BytesToRead->QuadPart -= FirstBlockDataDone;

            if (FirstBlockDataDone != FirstBlockDataToRead)
            {
                if (bVerbose)
                    fprintf(stderr, ", %.4g %s missing (%s:%u).\r\n",
                        TO_h(BytesToRead->QuadPart),
                        TO_p(BytesToRead->QuadPart),
                        __FILE__, __LINE__);
                Exception(XE_ARCHIVE_TRUNC);
            }

            dwBytesRead += FirstBlockDataDone;
        }
    }

    if (bVerbose)
    {
        if (header->dwStreamNameSize > 0)
            oem_printf(stderr,
                ", stream='%1!.*ws!'",
                header->dwStreamNameSize >> 1,
                header->cStreamName);

        if ((header->dwStreamId == BACKUP_SPARSE_BLOCK) &&
            (header->dwStreamAttributes == STREAM_SPARSE_ATTRIBUTE) &&
            (header->Size.QuadPart >= sizeof(LARGE_INTEGER)))
        {
            PLARGE_INTEGER StartPosition = (PLARGE_INTEGER)
                (Buffer + HEADER_SIZE + header->dwStreamNameSize);

            fprintf(stderr, ", offset=0x%.8x%.8x]",
                StartPosition->HighPart, StartPosition->LowPart);
        }
        else if (header->dwStreamId == BACKUP_LINK)
        {
            PWSTR Target = (PWSTR)
                (Buffer + HEADER_SIZE + header->dwStreamNameSize);

            oem_printf(stderr,
                ", target='%1!.*ws!']",
                header->Size.LowPart >> 1,
                Target);
        }
        else
            fputs("]", stderr);
    }
}

bool
StrArc::WriteFileHardLinkFromArchive(HANDLE &hFile,
    DWORD dwBytesRead,
    PUNICODE_STRING Target,
    bool bSeekOnly)
{
    DWORD offset = GetDataOffset();

    // If relative name is longer than read buffer, that is an archive
    // format error. Relative name should fit in one buffered read.
    if (dwBytesRead != (header->Size.QuadPart + offset))
    {
        if (bVerbose)
            oem_printf(stderr,
                " incomplete stream: Possibly corrupt or "
                "truncated archive.%%n"
                "The link '%1!wZ!' will not be restored.%%n",
                Target);
        else
            oem_printf(stderr, "strarc: Error in archive when extracting "
                "%1!wZ!%%n",
                Target);

        bSeekOnly = true;
    }

    UNICODE_STRING LinkName;
    InitCountedUnicodeString(&LinkName,
        (PWSTR)(Buffer + offset),
        (USHORT)header->Size.LowPart);

    HANDLE hSource = INVALID_HANDLE_VALUE;

    if (bHardLinkSupport && !bSeekOnly)
    {
        if (!NativeDeleteFile(hFile))
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "strarc: Cannot remove temporary file '%1!wZ!'. This "
                "may cause hard link restore to fail. Error: "
                "%2%%n",
                Target, errmsg);
        }

        NtClose(hFile);
        hFile = INVALID_HANDLE_VALUE;

        hSource = NativeOpenFile(RootDirectory,
            &LinkName,
            0,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
            dwCreateOption);

        if (hSource == INVALID_HANDLE_VALUE)
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "strarc: Cannot open '%1!wZ!' to restore link: "
                "%2%%n",
                &LinkName, errmsg);
            oem_printf(stderr,
                "The link '%1!ws!' will not be restored.%%n",
                Target->Length >> 1, Target->Buffer);
            return true;
        }

        if (CreateHardLinkToOpenFile(hSource,
            RootDirectory,
            Target,
            TRUE))
        {
            NtClose(hSource);
            ++FileCounter;
            return true;
        }
    }

    if (bVerbose)
        if (bHardLinkSupport)
        {
            WErrMsgA errmsg;
            oem_printf(stderr, ", link failed: %1%%n"
                "Copying instead",
                errmsg);
        }
        else
            fputs(", copying", stderr);
    else
        if (bHardLinkSupport)
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "Failed to create hard link '%1!wZ!': "
                "%2%%n",
                Target, errmsg);
            oem_printf(stderr,
                "Copying from '%1!wZ!' instead.%%n",
                &LinkName);
        }

    if (bSeekOnly)
        return true;

    // We need to get a handle with read access to copy the file instead of
    // hard linking
    if (hSource != INVALID_HANDLE_VALUE)
        NtClose(hSource);

    hSource = NativeOpenFile(RootDirectory,
        &LinkName,
        GENERIC_READ,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE,
        FILE_NON_DIRECTORY_FILE |
        FILE_SEQUENTIAL_ONLY |
        FILE_OPEN_FOR_BACKUP_INTENT);

    if (hSource == INVALID_HANDLE_VALUE)
    {
        WErrMsgA errmsg;
        oem_printf(stderr,
            "strarc: Cannot open '%1!wZ!' for copying: %2%%n",
            &LinkName, errmsg);
        oem_printf(stderr,
            "The file '%1!wZ!' will not be restored.%%n",
            Target);
        return true;
    }

    // Re-open target file in case we closed it above
    hFile = NativeCreateFile(RootDirectory,
        Target,
        GENERIC_READ | GENERIC_WRITE | WRITE_DAC |
        WRITE_OWNER | DELETE | READ_CONTROL | ACCESS_SYSTEM_SECURITY,
        0,
        NULL,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        dwExtractCreation,
        dwCreateOption,
        TRUE);
    
    if (hFile == INVALID_HANDLE_VALUE)
    {
        WErrMsgA errmsg;
        oem_printf(stderr,
            "strarc: Cannot create '%1!wZ!' for copying: %2%%n",
            Target, errmsg);
        return true;
    }

    if (!BackupCopyFile(hSource, hFile, &LinkName, Target))
    {
        WErrMsgA errmsg;

        if (bHardLinkSupport)
        {
            oem_printf(stderr,
                "strarc: %1%%n"
                "Failed both linking and copying '%2!wZ!'",
                errmsg,
                &LinkName);

            oem_printf(stderr,
                " to '%1!wZ!'.%%n",
                Target);
        }
        else
        {
            oem_printf(stderr,
                "strarc: %1%%n"
                "Failed to copy '%2!wZ!' to",
                errmsg,
                &LinkName);

            oem_printf(stderr,
                " '%1!wZ!'.%%n",
                Target);
        }

        if (!NativeDeleteFile(hFile))
        {
            WErrMsgA errmsg_nativedelete;
            oem_printf(stderr,
                "strarc: Cannot remove temporary file '%1!wZ!': %2%%n",
                Target, (LPCSTR)errmsg_nativedelete);
        }

        NtClose(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }
    else if (bVerbose)
        fputs(", Ok", stderr);

    NtClose(hSource);

    return true;
}

bool
StrArc::WriteFileDataStreamFromArchive(HANDLE &hFile,
    DWORD dwBytesRead,
    PLARGE_INTEGER BytesToRead,
    UNICODE_STRING File,
    bool &bSeekOnly)
{
    LPVOID lpCtx = NULL;

    if ((!bSeekOnly) &&
        (header->dwStreamId == BACKUP_SPARSE_BLOCK) &&
        (header->dwStreamAttributes == STREAM_SPARSE_ATTRIBUTE))
    {
        PLARGE_INTEGER StartPosition = (PLARGE_INTEGER)
            (Buffer + HEADER_SIZE + header->dwStreamNameSize);

        if ((SetFilePointer(hFile, StartPosition->LowPart,
            &StartPosition->HighPart, FILE_BEGIN) ==
            INVALID_SET_FILE_POINTER) ?
            (GetLastError() != NO_ERROR) : false)
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "strarc: Seek error in '%1!wZ!': %2%%n",
                &File, errmsg);
            bSeekOnly = true;
        }

        if (!SetEndOfFile(hFile))
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "strarc: Size error in '%1!wZ!': %2%%n",
                &File, errmsg);
            bSeekOnly = true;
        }
    }

    if (!bSeekOnly && !BackupWrite(hFile, Buffer, dwBytesRead,
        &dwBytesRead, FALSE, bProcessSecurity, &lpCtx))
    {
        WErrMsgA errmsg;
        oem_printf(stderr,
            "strarc: Cannot write '%1!wZ!': %2%%n",
            &File, errmsg);
        
        bSeekOnly = true;

        if (!NativeDeleteFile(hFile))
        {
            WErrMsgA errmsg_nativedelete;
            oem_printf(stderr,
                "strarc: Cannot remove temporary file '%1!wZ!': "
                "%2%%n",
                &File, (LPCSTR)errmsg_nativedelete);
        }
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
                if (!NativeDeleteFile(hFile))
                {
                    WErrMsgA errmsg;
                    oem_printf
                        (stderr,
                            "strarc: Cannot remove temporary file '%1!wZ!': "
                            "%2%%n",
                            &File, errmsg);
                }
                CloseHandle(hFile);
            }
            return false;
        }

        dwBytesRead = ReadArchive(Buffer,
            BytesToRead->QuadPart >
            (LONGLONG)dwBufferSize ?
            dwBufferSize : BytesToRead->LowPart);

        BytesToRead->QuadPart -= dwBytesRead;

        if (dwBytesRead == 0)
        {
            if (bVerbose)
                fprintf(stderr, ", %.4g %s missing (%s:%u).\r\n",
                    TO_h(BytesToRead->QuadPart),
                    TO_p(BytesToRead->QuadPart),
                    __FILE__, __LINE__);

            Exception(XE_ARCHIVE_TRUNC);
        }

        if (!bSeekOnly && !BackupWrite(hFile, Buffer, dwBytesRead, &dwBytesRead, FALSE,
            bProcessSecurity, &lpCtx))
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "strarc: Restore of '%1!wZ!' incomplete: %2%%n",
                &File, errmsg);

            if (lpCtx != NULL)
                BackupWrite(NULL, NULL, 0, NULL, TRUE, FALSE, &lpCtx);

            if (hFile != INVALID_HANDLE_VALUE)
            {
                if (!NativeDeleteFile(hFile))
                {
                    WErrMsgA errmsg_nativedelete;
                    oem_printf
                    (stderr,
                        "strarc: Cannot remove temporary file '%1!wZ!': "
                        "%2%%n",
                        &File, (LPCSTR)errmsg_nativedelete);
                }
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
StrArc::WriteFileAlternateStreamsFromArchive(DWORD dwBytesRead,
    PLARGE_INTEGER BytesToRead,
    ULONG StreamFileAttributes,
    PUNICODE_STRING FileBaseName,
    bool bSeekOnly)
{
    for (;;)
    {
        UNICODE_STRING complete_name = *FileBaseName;

        UNICODE_STRING stream_name;
        InitCountedUnicodeString(&stream_name,
            header->cStreamName,
            (USHORT)header->dwStreamNameSize);

        NTSTATUS status =
            RtlAppendUnicodeStringToString(&complete_name,
                &stream_name);

        if (!NT_SUCCESS(status))
        {
            oem_printf(stderr,
                "strarc: Bad path name '%1!wZ!",
                FileBaseName);

            oem_printf(stderr,
                ":%1!wZ!%%n",
                &stream_name);

            bSeekOnly = true;
        }

        HANDLE hStream;

        DWORD desired_access =
            FILE_GENERIC_READ | FILE_GENERIC_WRITE | WRITE_DAC |
            WRITE_OWNER | DELETE | READ_CONTROL | ACCESS_SYSTEM_SECURITY;

        for (;;)
        {
            hStream =
                NativeCreateFile(RootDirectory,
                    &complete_name,
                    desired_access,
                    0,
                    NULL,
                    NULL,
                    StreamFileAttributes,
                    FILE_SHARE_READ |
                    FILE_SHARE_DELETE,
                    FILE_SUPERSEDE,
                    dwCreateOption |
                    FILE_RANDOM_ACCESS,
                    FALSE);

            if (hStream == INVALID_HANDLE_VALUE)
            {
                switch (GetLastError())
                {
                case ERROR_ACCESS_DENIED:
                    if (desired_access & ACCESS_SYSTEM_SECURITY)
                    {
                        desired_access &= ~ACCESS_SYSTEM_SECURITY;

                        continue;
                    }

                    if (desired_access & (WRITE_DAC |
                        WRITE_OWNER | READ_CONTROL))
                    {
                        desired_access &= ~(WRITE_DAC |
                            WRITE_OWNER | READ_CONTROL);

                        continue;
                    }

                    break;
                }
            }

            break;
        }

        if (hStream == INVALID_HANDLE_VALUE)
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "Error restoring stream '%1!wZ!': %2%%n",
                &complete_name, errmsg);

            bSeekOnly = true;
        }

        if ((!bSeekOnly) &&
            bRestoreCompression &&
            (header->dwStreamAttributes & STREAM_SPARSE_ATTRIBUTE))
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

        if ((!bSeekOnly) && (header->Size.QuadPart > 0) &&
            (header->dwStreamAttributes == STREAM_NORMAL_ATTRIBUTE))
        {
            DWORD offset = GetDataOffset();
            LPBYTE data = Buffer + offset;
            DWORD size = dwBytesRead - offset;

            for (;;)
            {
                DWORD dwIO;
                BOOL bResult = WriteFile(hStream, data, size, &dwIO, NULL);

                if ((!bResult) || (dwIO != size))
                {
                    WErrMsgA errmsg;
                    oem_printf(stderr,
                        "Error writing stream data '%1!wZ!': %2%%n",
                        &complete_name, errmsg);

                    if (!NativeDeleteFile(hStream))
                    {
                        WErrMsgA errmsg_nativedelete;
                        oem_printf
                            (stderr,
                                "Error removing temporary stream '%1!wZ!': "
                                "%2%%n",
                                &complete_name, (LPCSTR)errmsg_nativedelete);
                    }
                    SkipArchive(BytesToRead);
                    break;
                }

                if (BytesToRead->QuadPart == 0)
                    break;

                if (BytesToRead->QuadPart > dwBufferSize)
                    size = dwBufferSize;
                else
                    size = BytesToRead->LowPart;

                dwBytesRead = ReadArchive(Buffer, size);

                if (dwBytesRead != size)
                {
                    fprintf(stderr,
                        "strarc: Incomplete stream: %u bytes missing.\n",
                        size - dwBytesRead);
                    Exception(XE_ARCHIVE_TRUNC);
                }

                data = Buffer;
                BytesToRead->QuadPart -= dwBytesRead;
            }
        }
        else
            SkipArchive(BytesToRead);

        for (;;)
        {
            dwBytesRead = ReadStreamHeader();

            if (dwBytesRead == 0)
            {
                if (hStream != INVALID_HANDLE_VALUE)
                    NtClose(hStream);

                return false;
            }

            if (dwBytesRead != HEADER_SIZE)
            {
                fprintf
                    (stderr,
                        "strarc: Incomplete stream header: %u bytes missing.\n",
                        HEADER_SIZE - dwBytesRead);

                if (hStream != INVALID_HANDLE_VALUE)
                    NtClose(hStream);

                Exception(XE_ARCHIVE_TRUNC);
            }

            if (IsNewFileHeader())
            {
                if (hStream != INVALID_HANDLE_VALUE)
                    NtClose(hStream);

                return true;
            }

            FillEntireBuffer(dwBytesRead,
                BytesToRead);

            // Another named alternate stream follows. Switch to restoring
            // that one.
            if (header->dwStreamNameSize > 0)
                break;

            if (!bSeekOnly)
            {
                bool bResult =
                    WriteFileDataStreamFromArchive(hStream,
                        dwBytesRead,
                        BytesToRead,
                        complete_name,
                        bSeekOnly);

                if (!bResult)
                {
                    if (!NativeDeleteFile(hStream))
                    {
                        WErrMsgA errmsg;
                        oem_printf
                            (stderr,
                                "Error removing temporary stream '%1!wZ!': %2%%n",
                                &complete_name, errmsg);
                    }
                    if (hStream != INVALID_HANDLE_VALUE)
                        NtClose(hStream);

                    return false;
                }
            }
        }

        NtClose(hStream);
    }
}

bool
StrArc::WriteFileFromArchive(PUNICODE_STRING File,
    HANDLE &hFile,
    bool &bSeekOnly)
{
    DWORD dwBytesRead;

    dwBytesRead = ReadStreamHeader();

    for (;;)
    {
        YieldSingleProcessor();

        if (bCancel)
        {
            if (hFile != INVALID_HANDLE_VALUE)
            {
                if (!NativeDeleteFile(hFile))
                {
                    WErrMsgA errmsg;
                    oem_printf
                        (stderr,
                            "strarc: Cannot remove temporary file '%1!wZ!': %2%%n",
                            File, errmsg);
                }
                CloseHandle(hFile);
            }
            return false;
        }

        if (dwBytesRead == 0)
            return true;

        if (dwBytesRead < HEADER_SIZE)
        {
            fprintf(stderr,
                "strarc: Incomplete stream header: %u bytes missing.\n",
                HEADER_SIZE - dwBytesRead);
            Exception(XE_ARCHIVE_TRUNC);
        }

        if (IsNewFileHeader())
            return true;

        LARGE_INTEGER BytesToRead;

        FillEntireBuffer(dwBytesRead,
            &BytesToRead);

        bool restore_result;
        if ((header->dwStreamId == BACKUP_LINK) && (!bSeekOnly))
        {
            restore_result =
                WriteFileHardLinkFromArchive(hFile,
                    dwBytesRead,
                    File,
                    bSeekOnly);

            dwBytesRead = ReadStreamHeader();
        }
        else if ((!bSeekOnly) &&
            (header->dwStreamId == BACKUP_ALTERNATE_DATA))
        {
            // BackupWrite is sadly broken in certain scenarios regarding named
            // alternate streams, particularly sparse streams, so we do our own
            // handling of alternate data streams.

            if (WriteFileAlternateStreamsFromArchive(dwBytesRead,
                &BytesToRead,
                FILE_ATTRIBUTE_NORMAL,
                File,
                bSeekOnly))
                dwBytesRead = HEADER_SIZE;
            else
                dwBytesRead = 0;

            restore_result = true;
        }
        else
        {
            restore_result =
                WriteFileDataStreamFromArchive(hFile,
                    dwBytesRead,
                    &BytesToRead,
                    *File,
                    bSeekOnly);

            dwBytesRead = ReadStreamHeader();
        }

        if (!restore_result)
            return false;
    }
}

/* File         - Name of file to restore with path relative to working
*                directory. File name must not be empty
* FileInfo     - Structure with file attributes and file times to restore.
* ShortName    - The 8.3 name to restore (max 13 characters, without path).
This parameter can be NULL or point to an empty string if no
short name is to be restored.
*/
bool
StrArc::RestoreFile(PUNICODE_STRING File,
    const PBY_HANDLE_FILE_INFORMATION FileInfo,
    PUNICODE_STRING ShortName)
{
    bool bSeekOnly = false;

    if (ShortName != NULL && ShortName->Length == 0)
    {
        ShortName = NULL;
    }

    // Special handling of the -8 command line switch. Copy the short name buffer
    // to the filename part of the full path buffer and set the short name buffer
    // pointer to NULL so that later code does not attempt to set a short name.
    if ((bRestoreShortNamesOnly) && (ShortName != NULL) &&
        !(FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        for (
            File->Length -= 2;
            File->Length > 0;
            File->Length -= 2)
        {
            if (File->Buffer[(File->Length >> 1) - 1] == L'\\')
                break;
        }

        if (!NT_SUCCESS(RtlAppendUnicodeStringToString(File, ShortName)))
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "strarc: Cannot restore '%1!wZ!': %2%%n",
                ShortName, errmsg);
            bSeekOnly = true;
        }
    }

    if (bVerbose)
    {
        oem_printf(stderr, "%1!wZ!, attr=%2 (%3!#x!)",
            File,
            GetFileAttributesDescription(FileInfo->dwFileAttributes),
            FileInfo->dwFileAttributes);

        if (ShortName != NULL)
            oem_printf(stderr, ", short='%1!wZ!'",
                ShortName);
    }

    bool bIncludeThis;
    ExcludedString(File, FileInfo, NULL, &bIncludeThis);

    FILE_BASIC_INFORMATION existing_file_info = { 0 };

    if (bIncludeThis)
    {
        HANDLE hFile =
            NativeOpenFile(RootDirectory,
                File,
                FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                dwCreateOption |
                FILE_OPEN_REPARSE_POINT);

        if (hFile == INVALID_HANDLE_VALUE &&
            GetLastError() == ERROR_INVALID_PARAMETER)
        {
            hFile =
                NativeOpenFile(RootDirectory,
                    File,
                    FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE |
                    FILE_SHARE_DELETE,
                    dwCreateOption);
        }

        if (hFile != INVALID_HANDLE_VALUE)
        {
            IO_STATUS_BLOCK io_status;
            NTSTATUS status =
                NtQueryInformationFile(hFile,
                    &io_status,
                    &existing_file_info,
                    sizeof(existing_file_info),
                    FileBasicInformation);

            if (NT_SUCCESS(status))
            {
                if (bVerbose)
                    fprintf(stderr,
                        ", existing attr=%s (%#x)",
                        GetFileAttributesDescription
                        (existing_file_info.FileAttributes),
                        existing_file_info.FileAttributes);

                if (bOverwriteArchived &&
                    (existing_file_info.FileAttributes & FILE_ATTRIBUTE_ARCHIVE))
                {
                    bIncludeThis = false;
                }
                else if (bOverwriteOlder)
                {
                    if (((~existing_file_info.FileAttributes) &
                        FILE_ATTRIBUTE_DIRECTORY) &&
                        (CompareFileTime((LPFILETIME)
                            &existing_file_info.LastWriteTime,
                            &FileInfo->ftLastWriteTime) >= 0))
                    {
                        bIncludeThis = false;
                    }

                }
            }
            else
            {
                WErrMsgA errmsg(RtlNtStatusToDosError(status));
                oem_printf(stderr,
                    "strarc: Error querying existing '%1!wZ': %2%%n",
                    File, errmsg);
            }

            if ((bIncludeThis) &&
                (existing_file_info.FileAttributes & FILE_ATTRIBUTE_READONLY))
            {
                existing_file_info.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
                status =
                    NtSetInformationFile(hFile,
                        &io_status,
                        &existing_file_info,
                        sizeof(existing_file_info),
                        FileBasicInformation);
                if (!NT_SUCCESS(status))
                {
                    WErrMsgA errmsg(RtlNtStatusToDosError(status));
                    oem_printf
                        (stderr,
                            "strarc: Error resetting read-only attribute on existing "
                            "'%1!wZ': %2%%n",
                            File, errmsg);
                }
            }

            NtClose(hFile);
        }
        else
        {
            switch (GetLastError())
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                break;

            default:
            {
                WErrMsgA errmsg;
                oem_printf(stderr,
                    "strarc: Error opening existing '%1!wZ': %2%%n",
                    File, errmsg);
            }
            }

            if (bFreshenExisting)
                bIncludeThis = false;
        }
    }

    if (bVerbose)
    {
        if (!bIncludeThis)
            fputs(", Skipping", stderr);
    }
    else if (bTestMode || bListFiles)
        if (bIncludeThis)
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

    HANDLE hFile = INVALID_HANDLE_VALUE;

    if (bTestMode || !bIncludeThis)
    {
        bSeekOnly = true;
    }
    else if (FileInfo->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        if (!bSeekOnly)
        {
            DWORD desired_access =
                FILE_READ_ATTRIBUTES |      // Needed to read and compare existing attributes
                FILE_TRAVERSE |
                FILE_LIST_DIRECTORY |
                FILE_WRITE_ATTRIBUTES |     // Needed to restore attributes
                FILE_WRITE_EA |             // Needed to restore extended attributes
                DELETE |		    // Needed to restore short name (will fail if any app has this dir as current)
                FILE_READ_DATA |            // Needed to restore compression attribute
                FILE_WRITE_DATA |	    // Needed to restore compression attribute
                WRITE_DAC | WRITE_OWNER |
                READ_CONTROL |
                ACCESS_SYSTEM_SECURITY;	    // Needed to restore security

            for (;;)
            {
                // DELETE permission is needed to set short filename. However, a
                // directory cannot be opened with the DELETE permission if
                // another process has set the directory or any subdirs as it's
                // current directory. Therefore we first try with DELETE and then
                // without if the first try fails.

                // FILE_READ_DATA | FILE_WRITE_DATA is needed to restore
                // compression attribute on newer versions. On older versions
                // this access is not supported for directories and would
                // fail. Therefore we first try with
                // FILE_READ_DATA | FILE_WRITE_DATA and then without if the
                // first try fails.

                hFile = NativeCreateDirectory(RootDirectory,
                    File,
                    desired_access,
                    0,
                    NULL,
                    FileInfo->dwFileAttributes &
                    ~FILE_ATTRIBUTE_READONLY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE |
                    FILE_SHARE_DELETE,
                    dwCreateOption |
                    ((existing_file_info.FileAttributes &
                        FILE_ATTRIBUTE_REPARSE_POINT) ?
                        FILE_OPEN_REPARSE_POINT : 0),
                    TRUE);

                if (hFile == INVALID_HANDLE_VALUE)
                {
                    switch (GetLastError())
                    {
                    case ERROR_ACCESS_DENIED:
                    case ERROR_SHARING_VIOLATION:
                    case ERROR_PRIVILEGE_NOT_HELD:
                        if (desired_access & DELETE)
                        {
                            desired_access &= ~DELETE;
                            
                            continue;
                        }

                        if (desired_access & ACCESS_SYSTEM_SECURITY)
                        {
                            desired_access &= ~ACCESS_SYSTEM_SECURITY;

                            continue;
                        }

                        if (desired_access & (WRITE_DAC |
                            WRITE_OWNER | READ_CONTROL))
                        {
                            desired_access &= ~(WRITE_DAC |
                                WRITE_OWNER | READ_CONTROL);

                            continue;
                        }

                        break;

                    case ERROR_INVALID_PARAMETER:
                        if (desired_access &
                            (FILE_READ_DATA | FILE_WRITE_DATA))
                        {
                            desired_access &=
                                ~(FILE_READ_DATA | FILE_WRITE_DATA);

                            continue;
                        }

                        break;
                    }
                }

                break;
            }
        }
    }
    else if (!bSeekOnly)
    {
        DWORD desired_access =
            FILE_GENERIC_READ | FILE_GENERIC_WRITE |
            WRITE_DAC | WRITE_OWNER | DELETE |
            READ_CONTROL | ACCESS_SYSTEM_SECURITY;

        for (;;)
        {
            hFile = NativeCreateFile(RootDirectory,
                File,
                desired_access,
                0,
                NULL,
                NULL,
                FileInfo->dwFileAttributes &
                ~FILE_ATTRIBUTE_READONLY,
                FILE_SHARE_READ | FILE_SHARE_DELETE,
                dwExtractCreation,
                dwCreateOption |
                ((existing_file_info.FileAttributes &
                    FILE_ATTRIBUTE_REPARSE_POINT) ?
                    FILE_OPEN_REPARSE_POINT : 0),
                TRUE);

            if (hFile == INVALID_HANDLE_VALUE)
            {
                switch (GetLastError())
                {
                case ERROR_ACCESS_DENIED:
                case ERROR_PRIVILEGE_NOT_HELD:
                    if (desired_access & ACCESS_SYSTEM_SECURITY)
                    {
                        desired_access &= ~ACCESS_SYSTEM_SECURITY;

                        continue;
                    }

                    if (desired_access & (WRITE_DAC |
                        WRITE_OWNER | READ_CONTROL))
                    {
                        desired_access &= ~(WRITE_DAC |
                            WRITE_OWNER | READ_CONTROL);

                        continue;
                    }

                    break;
                }
            }

            break;
        }
    }

    if ((!bTestMode) && bIncludeThis && (hFile == INVALID_HANDLE_VALUE))
    {
        WErrMsgA errmsg;
        oem_printf(stderr, "strarc: Cannot create '%1!wZ!': %2%%n",
            File, errmsg);
        bSeekOnly = true;
    }

    if ((existing_file_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        (hFile != INVALID_HANDLE_VALUE) && (!bSeekOnly))
    {
        REPARSE_GUID_DATA_BUFFER ReparseData = { 0 };

        ReparseData.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
        ReparseData.ReparseDataLength = 0;

        if (bVerbose)
            fputs(", Resetting reparse point", stderr);

        DWORD dwIO;
        if (!DeviceIoControl(hFile,
            FSCTL_DELETE_REPARSE_POINT,
            &ReparseData,
            REPARSE_GUID_DATA_BUFFER_HEADER_SIZE,
            NULL,
            0,
            &dwIO,
            NULL))
        {
            WErrMsgA errmsg;
            oem_printf(stderr,
                "strarc: Cannot reset existing reparse point '%1!wZ!': "
                "%2%%n",
                File, errmsg);
        }
        else if (bVerbose)
            fputs(", Ok", stderr);

        existing_file_info.FileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
    }

    if ((FileInfo->dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) &&
        (hFile != INVALID_HANDLE_VALUE) && (!bSeekOnly) && bRestoreCompression)
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

    if ((!bSkipShortNames) &&
        (ShortName != NULL) &&
        (hFile != INVALID_HANDLE_VALUE) &&
        (!bSeekOnly))
        if (!SetShortFileName(hFile, ShortName))
            if (bVerbose)
            {
                WErrMsgA errmsg;
                oem_printf(stderr,
                    "%%n"
                    "strarc: Cannot set short name on '%1!wZ!': %2%%n",
                    File, errmsg);
            }
            else if (!bNoShortNameWarnings)
            {
                switch (GetLastError())
                {
                case ERROR_INVALID_PARAMETER:
                case ERROR_BAD_LENGTH:
                case ERROR_MR_MID_NOT_FOUND:
                    break;
                default:
                {
                    WErrMsgA errmsg;
                    oem_printf(stderr,
                        "strarc: Cannot set short name on '%1!wZ!': %2%%n",
                        File, errmsg);
                }
                }
            }

    if (!WriteFileFromArchive(File, hFile, bSeekOnly))
        return false;

    if ((!bSeekOnly) && (hFile != INVALID_HANDLE_VALUE))
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
                        "strarc: %1ompressing '%2!wZ!'...",
                        usCommand ==
                        COMPRESSION_FORMAT_NONE ? "Dec" : "C",
                        File);

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
        }
        else if (bVerbose)
        {
            WErrMsgA errmsg;
            oem_printf(stderr, "Cannot get file attributes: %1", errmsg);
        }

        if (bProcessFileTimes || bProcessFileAttribs)
        {
            if (bProcessFileTimes)
            {
                existing_file_info.CreationTime =
                    *(PLARGE_INTEGER)&FileInfo->ftCreationTime;
                existing_file_info.LastAccessTime =
                    *(PLARGE_INTEGER)&FileInfo->ftLastAccessTime;
                existing_file_info.LastWriteTime =
                    *(PLARGE_INTEGER)&FileInfo->ftLastWriteTime;
                existing_file_info.ChangeTime =
                    *(PLARGE_INTEGER)&FileInfo->ftLastWriteTime;
            }
            else
            {
                existing_file_info.CreationTime.QuadPart = 0;
                existing_file_info.LastAccessTime.QuadPart = 0;
                existing_file_info.LastWriteTime.QuadPart = 0;
                existing_file_info.ChangeTime.QuadPart = 0;
            }
            if (bProcessFileAttribs)
                existing_file_info.FileAttributes = FileInfo->dwFileAttributes;
            else
                existing_file_info.FileAttributes = FILE_ATTRIBUTE_NORMAL;

            IO_STATUS_BLOCK io_status;
            NTSTATUS status =
                NtSetInformationFile(hFile,
                    &io_status,
                    &existing_file_info,
                    sizeof(existing_file_info),
                    FileBasicInformation);
            if (!NT_SUCCESS(status))
            {
                WErrMsgA errmsg(RtlNtStatusToDosError(status));
                oem_printf(stderr,
                    "strarc: Unable to set time stamps and/or attributes "
                    "on '%1!wZ!': %2%%n",
                    File, errmsg);
            }
        }
    }

    if (bVerbose)
        fputs("\r\n", stderr);

    if (((hFile != INVALID_HANDLE_VALUE) || bTestMode) && bIncludeThis)
        ++FileCounter;

    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);

    return true;
}

bool
StrArc::RestoreDirectoryTree()
{
    if (!ReadNextFileHeader())
        return false;

    for (;;)
    {
        YieldSingleProcessor();

        if ((header->dwStreamId != BACKUP_INVALID) ||
            (header->dwStreamAttributes != STRARC_MAGIC) ||
            ((header->Size.QuadPart != sizeof BY_HANDLE_FILE_INFORMATION) &&
                (header->Size.QuadPart != LONGLONG(sizeof BY_HANDLE_FILE_INFORMATION) + 26)) ||
                (header->dwStreamNameSize == 0) ||
            (header->dwStreamNameSize >= 65535) || (header->dwStreamNameSize & 1))
        {
            if (!ReadNextFileHeader())
                return true;
        }

        if (header->dwStreamNameSize > USHORT_MAX)
        {
            if (!ReadNextFileHeader())
                return true;
        }

        DWORD dwBytesToRead = header->dwStreamNameSize + header->Size.LowPart;
        if (dwBufferSize - HEADER_SIZE < dwBytesToRead)
        {
            Exception(XE_BAD_BUFFER);
        }

        DWORD dwBytesRead = ReadArchive(Buffer + HEADER_SIZE, dwBytesToRead);

        if (dwBytesRead != dwBytesToRead)
        {
            if (!ReadNextFileHeader())
                Exception(XE_ARCHIVE_TRUNC);

            continue;
        }

        UNICODE_STRING file_name;
        InitCountedUnicodeString(&file_name,
            header->cStreamName,
            (USHORT)header->dwStreamNameSize);

        RtlCopyUnicodeString(&FullPath, &file_name);

        BY_HANDLE_FILE_INFORMATION FileInfo;
        CopyMemory(&FileInfo, Buffer + HEADER_SIZE + header->dwStreamNameSize,
            sizeof FileInfo);

        WCHAR wczShortName[14] = L"";
        if (header->Size.QuadPart == LONGLONG(sizeof(BY_HANDLE_FILE_INFORMATION)) + 26)
        {
            CopyMemory(wczShortName, Buffer + HEADER_SIZE +
                header->dwStreamNameSize +
                sizeof BY_HANDLE_FILE_INFORMATION, 26);
            wczShortName[13] = 0;
        }
        UNICODE_STRING short_name;
        RtlInitUnicodeString(&short_name, wczShortName);

        if (!RestoreFile(&FullPath, &FileInfo, &short_name))
            return false;
    }
}
