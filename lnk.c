#ifndef _DLL
#define _DLL
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ntdll.h>
#include <winstrct.h>

#include "lnk.h"

#pragma comment(lib, "crthlp.lib")
#pragma comment(lib, "crtdll.lib")
#pragma comment(lib, "ntdll.lib")

BOOL
SetShortFileName(HANDLE hFile, LPCWSTR lpName)
{
  PFILE_NAME_INFORMATION FileNameData;
  DWORD dwLength = wcslen(lpName) * sizeof *lpName;
  NTSTATUS Status;
  IO_STATUS_BLOCK IoStatusBlock;

  FileNameData = halloc(dwLength + sizeof(FILE_NAME_INFORMATION) -
			sizeof(FileNameData->FileName));
  if (FileNameData == NULL)
    return FALSE;

  FileNameData->FileNameLength = dwLength;
  memcpy(FileNameData->FileName, lpName, dwLength);

  Status = NtSetInformationFile(hFile, &IoStatusBlock, FileNameData,
				dwLength + sizeof(FILE_NAME_INFORMATION) -
				sizeof(FileNameData->FileName),
				FileShortNameInformation);

  if (NT_SUCCESS(Status))
    return TRUE;
  else
    {
      SetLastError(RtlNtStatusToDosError(Status));
      return FALSE;
    }
}

BOOL
CreateHardLinkToOpenFile(HANDLE hFile, LPCWSTR lpTarget, BOOL bReplaceOk)
{
  UNICODE_STRING Target;
  PFILE_LINK_INFORMATION FileLinkData;
  NTSTATUS Status;
  IO_STATUS_BLOCK IoStatusBlock;

  if (!RtlDosPathNameToNtPathName_U(lpTarget, &Target, NULL, NULL))
    {
      SetLastError(ERROR_PATH_NOT_FOUND);
      return FALSE;
    }

  FileLinkData = halloc(Target.Length + sizeof(FILE_LINK_INFORMATION) -
			sizeof(FileLinkData->FileName));
  if (FileLinkData == NULL)
    return FALSE;

  FileLinkData->ReplaceIfExists = (BOOLEAN) bReplaceOk;
  FileLinkData->RootDirectory = NULL;
  FileLinkData->FileNameLength = Target.Length;
  memcpy(FileLinkData->FileName, Target.Buffer, Target.Length);

  Status = NtSetInformationFile(hFile, &IoStatusBlock, FileLinkData,
				Target.Length + sizeof(FILE_LINK_INFORMATION) -
				sizeof(FileLinkData->FileName),
				FileLinkInformation);

  hfree(FileLinkData);

  if (NT_SUCCESS(Status))
    {
      OBJECT_ATTRIBUTES ObjectAttributes;
      HANDLE Handle;
      
      InitializeObjectAttributes(&ObjectAttributes,
				 &Target,
				 OBJ_CASE_INSENSITIVE,
				 NULL,
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

      RtlFreeUnicodeString(&Target);
      return TRUE;
    }
  else
    {
      RtlFreeUnicodeString(&Target);
      SetLastError(RtlNtStatusToDosError(Status));
      return FALSE;
    }
}
