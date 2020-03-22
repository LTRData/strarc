#ifdef __cplusplus
extern "C" {
#endif

BOOL
SetShortFileName(HANDLE hFile,
		 PUNICODE_STRING Name);

BOOL
CreateHardLinkToOpenFile(HANDLE hFile,
			 HANDLE RootDirectoryHandle,
			 PUNICODE_STRING Target,
			 BOOLEAN ReplaceIfExists);

#ifdef __cplusplus
}
#endif

