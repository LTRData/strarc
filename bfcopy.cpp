/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2013
 *
 * bfcopy.cpp
 * Backup based copy file feature.
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

#include <process.h>

#include <windows.h>
#include <ntdll.h>
#include <intsafe.h>
#include <winstrct.h>

#include "strarc.hpp"

class FileCopyContext
{
  StrArc *SourceReadSession;
  StrArc *TargetWriteSession;

  PUNICODE_STRING SourceName;
  HANDLE SourceHandle;

  HANDLE SourceReadThreadHandle;

  PUNICODE_STRING TargetName;
  HANDLE TargetHandle;

  static
  unsigned
  CALLBACK
  SourceReadThread(void *lpCtx)
  {
    FileCopyContext *Context = (FileCopyContext *)lpCtx;

    DWORD dwResult = NO_ERROR;
    if (!Context->SourceReadSession->
	ReadFileStreamsToArchive(Context->SourceName,
				 Context->SourceHandle))
      dwResult = GetLastError();

    delete Context->SourceReadSession;
    Context->SourceReadSession = NULL;

    return dwResult;
  }

public:

  StrArc *GetSourceReadSession()
  {
    return SourceReadSession;
  }

  StrArc *GetTargetWriteSession()
  {
    return TargetWriteSession;
  }

  bool
  GetSourceReadThreadResult(DWORD dwMilliseconds = INFINITE)
  {
    WaitForSingleObject(SourceReadThreadHandle, dwMilliseconds);

    DWORD dwExitCode;
    if (!GetExitCodeThread(SourceReadThreadHandle, &dwExitCode))
      dwExitCode = GetLastError();

    CloseHandle(SourceReadThreadHandle);

    if (dwExitCode == NO_ERROR)
      return true;

    SetLastError(dwExitCode);
    return false;
  }

  bool
  StartSourceReadThread()
  {
    unsigned uiThreadId;
    SourceReadThreadHandle = (HANDLE)
      _beginthreadex(NULL, 0, SourceReadThread, this, 0, &uiThreadId);

    return SourceReadThreadHandle != INVALID_HANDLE_VALUE;
  }

  bool operator!()
  {
    if ((SourceReadSession == NULL) |
	(TargetWriteSession == NULL))
      return true;
    else
      return false;
  }

  FileCopyContext(StrArc *Template,
		  DWORD dwPipeBufferSize,
		  PUNICODE_STRING SourceName,
		  HANDLE SourceHandle,
		  PUNICODE_STRING TargetName,
		  HANDLE TargetHandle)
    : SourceReadSession(NULL),
      TargetWriteSession(NULL),
      SourceName(SourceName),
      SourceHandle(SourceHandle),
      TargetName(TargetName),
      TargetHandle(TargetHandle),
      SourceReadThreadHandle(INVALID_HANDLE_VALUE)
  {
    HANDLE hReadPipe;
    HANDLE hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, NULL, dwPipeBufferSize))
      return;

    SourceReadSession = Template->TemplateNew(hWritePipe);
    TargetWriteSession = Template->TemplateNew(hReadPipe);
  }

  ~FileCopyContext()
  {
    if (SourceReadSession != NULL)
      delete SourceReadSession;

    if (TargetWriteSession != NULL)
      delete TargetWriteSession;
  }
};

bool
StrArc::BackupCopyFile(HANDLE SourceHandle,
		       HANDLE TargetHandle,
		       PUNICODE_STRING SourceName,
		       PUNICODE_STRING TargetName)
{
  FileCopyContext Context(this,
			  dwBufferSize,
			  SourceName,
			  SourceHandle,
			  TargetName,
			  TargetHandle);

  if (!Context)
    return false;

  bool bSeekOnly = false;

  if (!Context.StartSourceReadThread())
    return false;

  bool bWriteResult = Context.GetTargetWriteSession()->
    WriteFileStreamsFromArchive(TargetName,
				TargetHandle,
				bSeekOnly);

  bool bReadResult =
    Context.GetSourceReadThreadResult();

  return bReadResult & bWriteResult;
}
