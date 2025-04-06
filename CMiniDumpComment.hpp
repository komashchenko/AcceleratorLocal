class CMiniDumpComment
{
public:
	DLL_CLASS_IMPORT CMiniDumpComment(int iSize, MemAllocAttribute_t allocAttribute = MemAllocAttribute_Unk0);
	DLL_CLASS_IMPORT ~CMiniDumpComment();
	DLL_CLASS_IMPORT const char* GetStartPointer();
	DLL_CLASS_IMPORT const char* GetEndPointer();
	DLL_CLASS_IMPORT const char* GetCurrentPointer();
	DLL_CLASS_IMPORT void EnsureOSDescription();
	DLL_CLASS_IMPORT int GetAvailableBufferSize();
	DLL_CLASS_IMPORT void Reset();
	DLL_CLASS_IMPORT void AppendOSComment();
	DLL_CLASS_IMPORT void AppendComment(const char* pszComment);
	DLL_CLASS_IMPORT void PrependComment(const char* pszComment);
	DLL_CLASS_IMPORT void AppendFormattedComment(const char* pszComment, ...) FMTFUNCTION(2, 3);
	DLL_CLASS_IMPORT bool EnsureEndsWithNumCharacters(char, int, bool);
	DLL_CLASS_IMPORT bool RemoveTrailingCharacters(char);
	DLL_CLASS_IMPORT void OnExceptionCaught();

private:
	[[maybe_unused]] char pad0[0x28];
};
static_assert(sizeof(CMiniDumpComment) == 0x28, "CMiniDumpComment - incorrect size on this compiler");

DLL_GLOBAL_IMPORT void LoggingSystem_GetLogCapture(CMiniDumpComment* pMiniDumpComment, bool bReversed);
