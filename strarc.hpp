/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2022
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

LPCSTR GetStreamIdDescription(DWORD StreamId);

LPCSTR GetStreamAttributesDescription(DWORD StreamAttributesId);

LPCSTR GetFileAttributesDescription(DWORD FileAttributes);

#ifdef _WIN64
#define MEMBERCALL
#else
#define MEMBERCALL __stdcall
#endif

#ifdef _WIN64
#pragma pack(push, 8)
#else
#pragma pack(push, 4)
#endif

class StrArc
{
    // This is a dummy member used to find position in memory where actual
    // data members area begins, after any rtti metadata or similar hidden
    // fields.
    INT_PTR FirstMember;

public:

    void* operator new(size_t size)
    {
        void *storage = LocalAlloc(LMEM_FIXED, size);
        if (NULL == storage)
            RaiseException((DWORD)STATUS_NO_MEMORY,
                EXCEPTION_NONCONTINUABLE,
                0,
                NULL);

        return storage;
    }

    void* operator new[](size_t size)
    {
        void *storage = LocalAlloc(LMEM_FIXED, size);
        if (NULL == storage)
            RaiseException((DWORD)STATUS_NO_MEMORY,
                EXCEPTION_NONCONTINUABLE,
                0,
                NULL);

        return storage;
    }

        void operator delete(void *ptr)
    {
        LocalFree(ptr);
    }

    void operator delete[](void *ptr)
    {
        LocalFree(ptr);
    }

        // This enumeration specifies various error codes that could be used when
        // calling Exception() on non-recoverable errors.
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
        XE_UNSUPPORTED_PLATFORM,
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

    // Information about currently raised exception, if any.
    struct StrArcExceptionData
    {
        // Application defined error code
        XError ErrorCode;

        // Win32 error code, that was returned by GetLastError()
        DWORD SysErrorCode;

        // Name of object that was processed when exception occurred
        LPCWSTR ObjectName;

        // System error message
        LPCWSTR SysErrorMessage;

        // Application errror message
        LPCWSTR ErrorMessage;
    };

private:

    // Constructor helper
    void
        Initialize();

    // BackupCopyFile function uses an internal class for working data. Make sure
    // that class can use private members of this class by declaring it as a
    // nested class.
    class FileCopyContext;

    WCHAR wczFullPathBuffer[32768];

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

    // This is the buffer used when calling the backup API functions.
    LPBYTE Buffer;
    DWORD dwBufferSize;

    // Information about currently raised exception, if any.
    StrArcExceptionData ExceptionData;

    // Because the buffer begins with a WIN32_STREAM_ID structure, this property
    // helps manipulating the header fields in the buffer.
    __declspec(property(get = GetHeader)) LPWIN32_STREAM_ID header;

    LPWIN32_STREAM_ID GetHeader()
    {
        return (LPWIN32_STREAM_ID)Buffer;
    }

    DWORD GetDataOffset()
    {
        return HEADER_SIZE + header->dwStreamNameSize;
    }

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
        WriteFileHardLinkFromArchive(HANDLE &hFile,
            DWORD dwBytesRead,
            PUNICODE_STRING Target,
            bool bSeekOnly);

    bool
        MEMBERCALL
        WriteFileDataStreamFromArchive(HANDLE &hFile,
            DWORD dwBytesRead,
            PLARGE_INTEGER BytesToRead,
            UNICODE_STRING File,
            bool &bSeekOnly);

    bool
        MEMBERCALL
        WriteFileAlternateStreamsFromArchive(DWORD dwBytesRead,
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
            const PBY_HANDLE_FILE_INFORMATION FileInfo,
            PUNICODE_STRING ShortName);

    void
        MEMBERCALL
        BackupDirectory(PUNICODE_STRING Path,
            HANDLE Handle);

    bool
        MEMBERCALL
        BackupFile(PUNICODE_STRING File,
            PUNICODE_STRING ShortName,
            bool bTraverseDirectories);

    bool
        IsNewFileHeader()
    {
        if ((header->dwStreamId == BACKUP_INVALID) &&
            (header->dwStreamAttributes == STRARC_MAGIC) &&
            ((header->Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION) ||
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
            Exception(XE_NOT_ENOUGH_MEMORY_FOR_LINK_TRACKER);

        LinkTrackerItems[(NodeNumber & 0xFF0) >> 4] = NewLinkInfo;

        return NULL;
    }

    // This function examines all paths and filenames on behalf of the backup and
    // restore routines. It identifies names containing strings matching the -i
    // or -e switches on the command line and returns matches in two optional
    // parameters.
    void
        ExcludedString(const PUNICODE_STRING Path,
            const PBY_HANDLE_FILE_INFORMATION FileInfo,
            bool *Excluded OPTIONAL,
            bool *Included OPTIONAL)
    {
        // This routine does not handle empty strings correctly.
        if (Path->Length == 0)
        {
            if (Excluded != NULL) *Excluded = false;
            if (Included != NULL) *Included = true;

            if (CustomFilter != NULL)
                CustomFilter(CustomFilterContext,
                    Path,
                    FileInfo,
                    Excluded,
                    Included);

            return;
        }

        LPCWSTR wcPathEnd = Path->Buffer + (Path->Length >> 1);

        if (dwExcludeStrings != 0)
        {
            DWORD dwExcludeString = dwExcludeStrings;
            LPCWSTR wczExcludeString = szExcludeStrings;

            do
            {
                DWORD_PTR dwExclStrLen = wcslen(wczExcludeString);

                for (LPCWSTR wcPathPtr = Path->Buffer;
                    (DWORD)(wcPathEnd - wcPathPtr) >= dwExclStrLen;
                    wcPathPtr++)
                    if (_wcsnicmp(wcPathPtr, wczExcludeString, dwExclStrLen) == 0)
                    {
                        if (Excluded != NULL) *Excluded = true;
                        if (Included != NULL) *Included = false;

                        if (CustomFilter != NULL)
                            CustomFilter(CustomFilterContext,
                                Path,
                                FileInfo,
                                Excluded,
                                Included);

                        return;
                    }

                wczExcludeString += dwExclStrLen + 1;
            } while (--dwExcludeString);
        }

        if (dwIncludeStrings == 0)
        {
            if (Excluded != NULL) *Excluded = false;
            if (Included != NULL) *Included = true;

            if (CustomFilter != NULL)
                CustomFilter(CustomFilterContext,
                    Path,
                    FileInfo,
                    Excluded,
                    Included);

            return;
        }

        DWORD dwIncludeString = dwIncludeStrings;
        LPCWSTR wczIncludeString = szIncludeStrings;

        do
        {
            DWORD_PTR dwInclStrLen = wcslen(wczIncludeString);

            for (LPCWSTR wcPathPtr = Path->Buffer;
                (DWORD)(wcPathEnd - wcPathPtr) >= dwInclStrLen;
                wcPathPtr++)
                if (_wcsnicmp(wcPathPtr, wczIncludeString, dwInclStrLen) == 0)
                {
                    if (Excluded != NULL) *Excluded = false;
                    if (Included != NULL) *Included = true;

                    if (CustomFilter != NULL)
                        CustomFilter(CustomFilterContext,
                            Path,
                            FileInfo,
                            Excluded,
                            Included);

                    return;
                }

            wczIncludeString += dwInclStrLen + 1;
        } while (--dwIncludeString);

        if (Excluded != NULL) *Excluded = false;
        if (Included != NULL) *Included = false;

        if (CustomFilter != NULL)
            CustomFilter(CustomFilterContext, Path, FileInfo, Excluded, Included);
    }

    // Copy source file to target file using backup functions.
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
                    Exception(XE_ARCHIVE_IO);
                }

            if (dwBytesRead == 0)
                return dwTotalBytes;

            dwTotalBytes += dwBytesRead;
            dwSize -= dwBytesRead;
            lpBuf += dwBytesRead;
        }

        return dwTotalBytes;
    }

    // This function reads a stream header within a restore operation for a file.
    // There could be several streams for each file.
    // If no more stream headers exist in input archive, this function zeroes
    // header memory and returns a value less than HEADER_SIZE.
    DWORD
        ReadStreamHeader()
    {
        DWORD dwBytesRead = ReadArchive(Buffer, HEADER_SIZE);

        if (dwBytesRead < HEADER_SIZE)
            memset(Buffer, 0, HEADER_SIZE);

        return dwBytesRead;
    }

    // This function reads a file header for a new file from the archive. If not
    // a valid header is found the function seeks forward in the archive until it
    // finds a valid header or EOF. If a valid header is found, true is returned,
    // otherwise false is returned.
    bool
        ReadNextFileHeader()
    {
        DWORD dwBytesRead = ReadArchive(Buffer, HEADER_SIZE);

        if (dwBytesRead < HEADER_SIZE)
            return false;

        if ((header->dwStreamId == BACKUP_INVALID) &&
            (header->dwStreamAttributes == STRARC_MAGIC) &&
            ((header->Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION) ||
            (header->Size.QuadPart == sizeof BY_HANDLE_FILE_INFORMATION + 26)) &&
            (header->dwStreamNameSize > 0) &&
            (header->dwStreamNameSize <= 65535) &&
            ((header->dwStreamNameSize & 1) == 0))
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

            MoveMemory(Buffer, Buffer + 1, HEADER_SIZE - 1);
            if (ReadArchive(Buffer + HEADER_SIZE - 1, 1) != 1)
            {
                fputs("strarc: Invalid data, unexpected end of archive.\r\n",
                    stderr);
                return false;
            }
        } while ((header->dwStreamId != BACKUP_INVALID) ||
            (header->dwStreamAttributes != STRARC_MAGIC) ||
            ((header->Size.QuadPart != sizeof BY_HANDLE_FILE_INFORMATION) &&
            (header->Size.QuadPart !=
                sizeof BY_HANDLE_FILE_INFORMATION + 26)) ||
                (header->dwStreamNameSize == 0) ||
            (header->dwStreamNameSize > 65535) ||
            (header->dwStreamNameSize & 1));

        return true;
    }

    // This function writes a specified block to the archive. If it is not
    // possible/ to write the complete block that is considered a fatal error and
    // Exception() is called causing the application to  display an error
    // message and exit.
    void
        WriteArchive(LPBYTE lpBuf, DWORD dwSize)
    {
        DWORD dwBytesWritten;

        while (dwSize > 0)
            if (!WriteFile(hArchive, lpBuf, dwSize, &dwBytesWritten, NULL))
                Exception(XE_ARCHIVE_IO);
            else if (dwBytesWritten == 0)
            {
                if (bVerbose)
                    fputs("\r\nWrite of archive raised EOF condition.\r\n", stderr);

                Exception(XE_ARCHIVE_TRUNC);
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
                (LONGLONG)dwBufferSize ?
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
                Exception(XE_ARCHIVE_TRUNC);
            }
        }

        return true;
    }

    bool
        InitializeBuffer(DWORD dwSize)
    {
        dwBufferSize = dwSize;
        Buffer = (LPBYTE)LocalAlloc(LPTR, dwBufferSize);
        return Buffer != NULL;
    }

    bool
        MEMBERCALL
        ReadFileStreamsToArchive(PUNICODE_STRING File,
            HANDLE hFile);

    bool
        MEMBERCALL
        WriteFileFromArchive(PUNICODE_STRING File,
            HANDLE &hFile,
            bool &bSeekOnly);

    void
        __declspec(noreturn) MEMBERCALL
        Exception(XError XE, LPCWSTR Name = NULL);

    LONGLONG FileCounter;
    DWORD dwExtractCreation;
    DWORD dwCreateOption;

    PROCESS_INFORMATION piFilter;

    // Flags set by Main() to corresponding command line switches, or similar.
    bool bVerbose;
    bool bListFiles;
    bool bLocal;
    bool bTestMode;
    bool bListOnly;
    BOOL bProcessSecurity;
    bool bProcessFileTimes;
    bool bProcessFileAttribs;
    bool bHardLinkSupport;
    bool bRestoreCompression;
    bool bSkipShortNames;
    bool bNoShortNameWarnings;
    bool bOverwriteOlder;
    bool bOverwriteArchived;
    bool bFreshenExisting;
    bool bRestoreShortNamesOnly;
    bool bBackupRegistrySnapshots;

    // Backup method for this session
    BackupMethods BackupMethod;

    // Strings used by ExcludedString method
    DWORD dwExcludeStrings;
    LPWSTR szExcludeStrings;
    DWORD dwIncludeStrings;
    LPWSTR szIncludeStrings;

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

    void
        MEMBERCALL
        SetIncludeExcludeStrings(LPDWORD dwStrings,
            LPWSTR *szStrings,
            LPCWSTR str);

protected:

    XError
        MEMBERCALL
        InitializeBuffer();

    bool
        MEMBERCALL
        OpenArchive(LPCWSTR wczFilename,
            bool bBackupMode,
            DWORD dwArchiveCreation);

    bool
        MEMBERCALL
        OpenFilterUtility(LPWSTR wczFilterCmd,
            bool bBackupMode);

    bool
        MEMBERCALL
        OpenWorkingDirectory(LPCWSTR wczStartDir,
            bool bBackupMode);

public:

    const StrArcExceptionData *
        GetExceptionData() const
    {
        return &ExceptionData;
    }

    LPWSTR
        GetExcludeStrings() const
    {
        return szExcludeStrings;
    }

    DWORD
        GetExcludeStringsCount() const
    {
        return dwExcludeStrings;
    }

    void
        SetExcludeStrings(LPCWSTR str)
    {
        SetIncludeExcludeStrings(&dwExcludeStrings,
            &szExcludeStrings,
            str);
    }

    LPWSTR
        GetIncludeStrings() const
    {
        return szIncludeStrings;
    }

    DWORD
        GetIncludeStringsCount() const
    {
        return dwIncludeStrings;
    }

    void
        SetIncludeStrings(LPCWSTR str)
    {
        SetIncludeExcludeStrings(&dwIncludeStrings,
            &szIncludeStrings,
            str);
    }

    // This member works like and is called by ExcludedString(). It provides a
    // way to extend file inclusion/exclusion filtering using a custom function.
    typedef
        void
        (CALLBACK CustomFilterCallback)(LPVOID Context,
            const PUNICODE_STRING Path,
            const PBY_HANDLE_FILE_INFORMATION FileInfo,
            bool *Excluded OPTIONAL,
            bool *Included OPTIONAL);

    typedef CustomFilterCallback *CustomFilterCallbackPtr;

    // This member works like and is called by ExcludedString(). It provides a
    // way to extend file inclusion/exclusion filtering using a custom function.
    CustomFilterCallbackPtr CustomFilter;

    LPVOID CustomFilterContext;

    bool
        BackupFile(LPCWSTR wczFile, bool bTraverseSubDirectories)
    {
        UNICODE_STRING file_name;
        RtlInitUnicodeString(&file_name, wczFile);

        if (file_name.Length > FullPath.MaximumLength)
        {
            fprintf(stderr, "strarc: The path is too long: '%ws'\n", wczFile);
            return false;
        }

        RtlCopyUnicodeString(&FullPath, &file_name);

        return BackupFile(&FullPath, NULL, bTraverseSubDirectories);
    }

    bool
        BackupFile(PUNICODE_STRING Path, bool bTraverseSubDirectories)
    {
        if (Path->Length > FullPath.MaximumLength)
        {
            fprintf(stderr,
                "strarc: The path is too long: '%wZ'\n",
                Path);
            return false;
        }

        RtlCopyUnicodeString(&FullPath, Path);

        return BackupFile(&FullPath, NULL, bTraverseSubDirectories);
    }

    void
        BackupFiles(int argc,
            LPWSTR *argv)
    {
        while (argc-- > 1)
        {
            YieldSingleProcessor();

            if (bCancel)
                break;

            BackupFile((argv++)[1], true);
        }
    }

    void
        MEMBERCALL
        BackupFilenamesFromStreamW(HANDLE hInputFile);

    void
        MEMBERCALL
        BackupFilenamesFromStreamA(HANDLE hInputFile);

    bool
        BackupCurrentDirectory()
    {
        FullPath.Length = 0;
        return BackupFile(&FullPath, NULL, true);
    }

    enum STRARC_OPERATION_MODE
    {
        STRARC_BACKUP_MODE,
        STRARC_RESTORE_MODE,
        STRARC_TEST_MODE
    };

    enum STRARC_OPERATIONAL_FLAGS
    {
        STRARC_FLAG_NONE = 0x00000001UL,
        STRARC_FLAG_VERBOSE = 0x00000002UL,
        STRARC_FLAG_LIST_FILES = 0x00000004UL,
        STRARC_FLAG_LIST_ONLY = 0x00000008UL,
        STRARC_FLAG_NO_FOLLOW_REPARSE = 0x00000010UL,
        STRARC_FLAG_NO_PROCESS_SECURITY = 0x00000020UL,
        STRARC_FLAG_NO_PROCESS_FILE_TIME = 0x00000040UL,
        STRARC_FLAG_NO_PROCESS_FILE_ATTRIBUTES = 0x00000080UL,
        STRARC_FLAG_NO_TRACK_HARD_LINKS = 0x00000100UL,
        STRARC_FLAG_NO_RESTORE_COMPRESSION = 0x00000200UL,
        STRARC_FLAG_NO_SHORT_NAMES = 0x00000400UL,
        STRARC_FLAG_NO_SHORT_NAME_WARNINGS = 0x00000800UL,
        STRARC_FLAG_OVERWRITE_OLDER = 0x00001000UL,
        STRARC_FLAG_OVERWRITE_ARCHIVED = 0x00002000UL,
        STRARC_FLAG_FRESHEN_EXISTING = 0x00004000UL,
        STRARC_FLAG_RESTORE_SHORT_NAMES_ONLY = 0x00008000UL,
        STRARC_FLAG_BACKUP_REGISTRY_SNAPSHOTS = 0x00010000UL
    };

    MEMBERCALL
        StrArc(STRARC_OPERATION_MODE OperationMode,
            STRARC_OPERATIONAL_FLAGS OperationalFlags = STRARC_FLAG_NONE,
            BackupMethods BackupMethod = BACKUP_METHOD_COPY,
            LPCWSTR wczArchiveFile = NULL,
            bool bAppendToArchive = false,
            DWORD dwBufferSize = DEFAULT_STREAM_BUFFER_SIZE,
            LPWSTR wczFilterCmd = NULL,
            LPCWSTR wczStartDir = NULL);

    MEMBERCALL
        StrArc();

    MEMBERCALL
        ~StrArc();

    static
        DWORDLONG
        MEMBERCALL
        GetLibraryVersion();

    LONGLONG
        GetFileCount()
    {
        return FileCounter;
    }

    bool bCancel;

    int
        MEMBERCALL
        Main(int argc, LPWSTR *argv);

    bool
        IsInitialized() const
    {
        return
            (hArchive != NULL) &&
            (Buffer != NULL) &&
            (RootDirectory != NULL);
    }

    bool
        MEMBERCALL
        CreateRegistrySnapshots();

    bool
        MEMBERCALL
        RestoreDirectoryTree();
};

#pragma pack(pop)
