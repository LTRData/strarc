#ifdef __cplusplus
extern "C" {
#endif

BOOL
SetShortFileName(HANDLE hFile, LPCWSTR lpName);

BOOL
CreateHardLinkToOpenFile(HANDLE hFile, LPCWSTR lpTarget, BOOL bReplaceOk);

#ifdef __cplusplus
}
#endif

