#include "CStrategyLoader.h"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
#ifdef _WIN32
	void* OpenLibrary(const std::string& strPath)
	{
		return reinterpret_cast<void*>(LoadLibraryW(std::filesystem::path(strPath).wstring().c_str()));
	}

	void* FindSymbol(void* pHandle, const char* pszName)
	{
		return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(pHandle), pszName));
	}

	void CloseLibrary(void* pHandle)
	{
		FreeLibrary(static_cast<HMODULE>(pHandle));
	}

	std::string LibraryError()
	{
		const DWORD nError = GetLastError();
		char buf[512] = {};
		const DWORD nLen = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, nError,
		                                  0, buf, sizeof(buf), nullptr);
		std::string strMsg(buf, nLen);
		while(!strMsg.empty() && (strMsg.back() == '\n' || strMsg.back() == '\r' || strMsg.back() == ' ' || strMsg.back() == '.'))
			strMsg.pop_back();
		return strMsg + " (error " + std::to_string(nError) + ")";
	}
#else
	void* OpenLibrary(const std::string& strPath)
	{
		return dlopen(strPath.c_str(), RTLD_NOW | RTLD_LOCAL);
	}

	void* FindSymbol(void* pHandle, const char* pszName)
	{
		return dlsym(pHandle, pszName);
	}

	void CloseLibrary(void* pHandle)
	{
		dlclose(pHandle);
	}

	std::string LibraryError()
	{
		const char* pszErr = dlerror();
		return pszErr ? pszErr : "unknown error";
	}
#endif
}

CStrategyLoader::CStrategyLoader():
	pHandle(nullptr),
	pfnDestroy(nullptr),
	pStrategy(nullptr)
{
}

CStrategyLoader::~CStrategyLoader()
{
	Unload();
}

bool CStrategyLoader::Load(const std::string& strPath)
{
	Unload();

	std::error_code ec;
	if(!std::filesystem::is_regular_file(strPath, ec))
	{
		strLastError = "Strategy not found: " + strPath;
		return false;
	}

	pHandle = OpenLibrary(strPath);
	if(!pHandle)
	{
		strLastError = "Library cannot be loaded: " + strPath + ": " + LibraryError();
		return false;
	}

	auto pfnVersion = reinterpret_cast<SXE_ApiVersionFn>(FindSymbol(pHandle, SXE_SYMBOL_API_VERSION));
	auto pfnCreate  = reinterpret_cast<SXE_CreateFn>(FindSymbol(pHandle, SXE_SYMBOL_CREATE));
	pfnDestroy      = reinterpret_cast<SXE_DestroyFn>(FindSymbol(pHandle, SXE_SYMBOL_DESTROY));

	std::string strMissing;
	if(!pfnVersion) strMissing += " " SXE_SYMBOL_API_VERSION;
	if(!pfnCreate)  strMissing += " " SXE_SYMBOL_CREATE;
	if(!pfnDestroy) strMissing += " " SXE_SYMBOL_DESTROY;
	if(!strMissing.empty())
	{
		strLastError = "Not an SXE strategy, missing functions:" + strMissing +
		               " (SXE_EXPORT_STRATEGY missing in the .cpp?)";
		Unload();
		return false;
	}

	const int nVersion = pfnVersion();
	if(nVersion != SXE_API_VERSION)
	{
		strLastError = "Strategy built with interface version " + std::to_string(nVersion) +
		               ", the engine uses version " + std::to_string(SXE_API_VERSION) + ": rebuild it";
		Unload();
		return false;
	}

	pStrategy = pfnCreate();
	if(!pStrategy)
	{
		strLastError = "SXE_Create did not create the strategy";
		Unload();
		return false;
	}

	strLastError.clear();
	return true;
}

void CStrategyLoader::Unload()
{
	if(pStrategy && pfnDestroy)
		pfnDestroy(pStrategy);
	pStrategy  = nullptr;
	pfnDestroy = nullptr;

	if(pHandle)
		CloseLibrary(pHandle);
	pHandle = nullptr;
}
