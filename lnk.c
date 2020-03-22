#ifndef _DLL
#define _DLL
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ntdll.h>
#include <winstrct.h>

#include <malloc.h>

#include "lnk.h"

BOOL
SetShortFileName(HANDLE hFile, PUNICODE_STRING Name)
{
  PFILE_NAME_INFORMATION FileNameData;
  NTSTATUS Status;
  IO_STATUS_BLOCK IoStatusBlock;

  FileNameData = malloc(Name->Length + sizeof(FILE_NAME_INFORMATION) -
			sizeof(FileNameData->FileName));
  if (FileNameData == NULL)
    return FALSE;

  FileNameData->FileNameLength = Name->Length;
  memcpy(FileNameData->FileName, Name->Buffer, Name->Length);

  Status = NtSetInformationFile(hFile, &IoStatusBlock, FileNameData,
				Name->Length + sizeof(FILE_NAME_INFORMATION) -
				sizeof(FileNameData->FileName),
				FileShortNameInformation);

  free(FileNameData);

  if (NT_SUCCESS(Status))
    return TRUE;
  else
    {
      SetLastError(RtlNtStatusToDosError(Status));
      return FALSE;
    }
}

BOOL
CreateHardLinkToOpenFile(HANDLE hFile,
			 HANDLE RootDirectory,
			 PUNICODE_STRING Target,
			 BOOLEAN ReplaceIfExists)
{
  PFILE_LINK_INFORMATION FileLinkData;
  NTSTATUS Status;
  IO_STATUS_BLOCK IoStatusBlock;

  FileLinkData = malloc(Target->Length + sizeof(FILE_LINK_INFORMATION) -
			sizeof(FileLinkData->FileName));
  if (FileLinkData == NULL)
    return FALSE;

  FileLinkData->ReplaceIfExists = ReplaceIfExists;
  FileLinkData->RootDirectory = RootDirectory;
  FileLinkData->FileNameLength = Target->Length;
  memcpy(FileLinkData->FileName, Target->Buffer, Target->Length);

  Status = NtSetInformationFile(hFile,
				&IoStatusBlock,
				FileLinkData,
				Target->Length +
				sizeof(FILE_LINK_INFORMATION) -
				sizeof(FileLinkData->FileName),
				FileLinkInformation);

  free(FileLinkData);

  if (NT_SUCCESS(Status))
    {
      // If successful, open newly created link just to resynchronize file
      // attributes and times, security desciptors etc from target file entry.

      OBJECT_ATTRIBUTES ObjectAttributes;
      HANDLE Handle;
      
      InitializeObjectAttributes(&ObjectAttributes,
				 Target,
				 0,
				 RootDirectory,
				 NULL);

      Status = NtOpenFile(&Handle,
			  FILE_READ_ATTRIBUTES,
			  &ObjectAttributes,
			  &IoStatusBlock,
			  FILE_SHARE_READ |
			  FILE_SHARE_WRITE |
			  FILE_SHARE_DELETE,
			  FILE_OPEN_FOR_BACKUP_INTENT);
      if (NT_SUCCESS(Status))
	NtClose(Handle);

      return TRUE;
    }
  else
    {
      SetLastError(RtlNtStatusToDosError(Status));
      return FALSE;
    }
}
