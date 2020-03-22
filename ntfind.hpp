#ifndef NtFileFinderBaseClassId
typedef FILE_BOTH_DIR_INFORMATION NtFileFinderBaseClass;
#define NtFileFinderBaseClassId FileBothDirectoryInformation
#endif

inline
VOID
InitCountedUnicodeString(PUNICODE_STRING Destination,
			 PWSTR Source,
			 USHORT SourceSize)
{
  Destination->Length = SourceSize;
  Destination->MaximumLength = SourceSize;
  Destination->Buffer = Source;
}

inline
HANDLE
NativeOpenFile(IN HANDLE RootDirectory,
	       IN PUNICODE_STRING Name,
	       IN ACCESS_MASK DesiredAccess,
	       IN ULONG ObjectOpenAttributes,
	       IN ULONG ShareAccess,
	       IN ULONG OpenOptions)
{
  OBJECT_ATTRIBUTES object_attributes;

  InitializeObjectAttributes(&object_attributes,
			     Name,
			     ObjectOpenAttributes,
			     RootDirectory,
			     NULL);

  IO_STATUS_BLOCK io_status;
  HANDLE handle;

  NTSTATUS status =
    NtOpenFile(&handle,
	       DesiredAccess | SYNCHRONIZE,
	       &object_attributes,
	       &io_status,
	       ShareAccess,
	       OpenOptions |
	       FILE_SYNCHRONOUS_IO_NONALERT);

  if (!NT_SUCCESS(status))
    {
      SetLastError(RtlNtStatusToDosError(status));
      return INVALID_HANDLE_VALUE;
    }

  return handle;
}

inline
HANDLE
NativeCreateFile(IN HANDLE RootDirectory,
		 IN PUNICODE_STRING Name,
		 IN ACCESS_MASK DesiredAccess,
		 IN ULONG ObjectOpenAttributes,
		 OUT PULONG_PTR CreationResult OPTIONAL,
		 IN PLARGE_INTEGER AllocationSize OPTIONAL,
		 IN ULONG FileAttributes,
		 IN ULONG ShareAccess,
		 IN ULONG CreateDisposition,
		 IN ULONG CreateOptions)
{
  OBJECT_ATTRIBUTES object_attributes;

  InitializeObjectAttributes(&object_attributes,
			     Name,
			     ObjectOpenAttributes,
			     RootDirectory,
			     NULL);

  IO_STATUS_BLOCK io_status;
  HANDLE handle;

  NTSTATUS status =
    NtCreateFile(&handle,
		 DesiredAccess | SYNCHRONIZE,
		 &object_attributes,
		 &io_status,
		 AllocationSize,
		 FileAttributes,
		 ShareAccess,
		 CreateDisposition,
		 CreateOptions |
		 FILE_SYNCHRONOUS_IO_NONALERT,
		 NULL,
		 0);

  if (!NT_SUCCESS(status))
    {
      SetLastError(RtlNtStatusToDosError(status));
      return INVALID_HANDLE_VALUE;
    }

  if (CreationResult != NULL)
    *CreationResult = io_status.Information;

  return handle;
}

inline
BOOL
NativeDeleteFile(HANDLE Handle)
{
  IO_STATUS_BLOCK io_status;

  FILE_DISPOSITION_INFORMATION info = { TRUE };

  NTSTATUS status =
    NtSetInformationFile(Handle,
			 &io_status,
			 &info,
			 sizeof(info),
			 FileDispositionInformation);

  if (!NT_SUCCESS(status))
    {
      SetLastError(RtlNtStatusToDosError(status));
      return FALSE;
    }

  return TRUE;
}

class NtFileFinder : public NtFileFinderBaseClass
{

private:

  WCHAR FileNameBuffer[32767];

  HANDLE DirHandle;

  NTSTATUS LastStatus;

public:

  /// cast to bool returns status of object.
  operator bool() const
  {
    return NT_SUCCESS(LastStatus);
  }

  DWORD GetLastError() const
  {
    return RtlNtStatusToDosError(LastStatus);
  }

  NTSTATUS GetLastStatus() const
  {
    return LastStatus;
  }

  NtFileFinder(HANDLE DirHandle,
	       PUNICODE_STRING FilePattern)
  {
    RtlZeroMemory(this, sizeof(*this));

    this->DirHandle = DirHandle;

    if (DirHandle == NULL)
      LastStatus = STATUS_INVALID_HANDLE;
    else
      Restart(FilePattern);
  }

  NtFileFinder(HANDLE DirHandle)
  {
    RtlZeroMemory(this, sizeof(*this));

    this->DirHandle = DirHandle;

    if (DirHandle == NULL)
      LastStatus = STATUS_INVALID_HANDLE;
    else
      Restart();
  }

  /// Finds first matching file.
  bool Restart()
  {
    return Restart(NULL);
  }

  /// Finds first matching file.
  bool Restart(PUNICODE_STRING FilePattern)
  {
    IO_STATUS_BLOCK IoStatusBlock;

    LastStatus =
      NtQueryDirectoryFile(DirHandle,
			   NULL,
			   NULL,
			   NULL,
			   &IoStatusBlock,
			   this,
			   sizeof(NtFileFinderBaseClass) +
			   sizeof(FileNameBuffer),
			   NtFileFinderBaseClassId,
			   TRUE,
			   FilePattern,
			   TRUE);

    return NT_SUCCESS(LastStatus);
  }

  /// Finds next matching file.
  bool Next()
  {
    if (DirHandle == NULL)
      return false;

    IO_STATUS_BLOCK IoStatusBlock;

    LastStatus =
      NtQueryDirectoryFile(DirHandle,
			   NULL,
			   NULL,
			   NULL,
			   &IoStatusBlock,
			   this,
			   sizeof(NtFileFinderBaseClass) +
			   sizeof(FileNameBuffer),
			   NtFileFinderBaseClassId,
			   TRUE,
			   NULL,
			   FALSE);

    return NT_SUCCESS(LastStatus);
  }
};
