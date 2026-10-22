#include "CStrategyLoader.h"

#include <dlfcn.h>
#include <filesystem>
#include <system_error>

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

	pHandle = dlopen(strPath.c_str(), RTLD_NOW | RTLD_LOCAL);
	if(!pHandle)
	{
		const char* pszErr = dlerror();
		strLastError = "Library cannot be loaded: " + std::string(pszErr ? pszErr : strPath);
		return false;
	}

	dlerror();
	auto pfnVersion = reinterpret_cast<SXE_ApiVersionFn>(dlsym(pHandle, SXE_SYMBOL_API_VERSION));
	auto pfnCreate  = reinterpret_cast<SXE_CreateFn>(dlsym(pHandle, SXE_SYMBOL_CREATE));
	pfnDestroy      = reinterpret_cast<SXE_DestroyFn>(dlsym(pHandle, SXE_SYMBOL_DESTROY));

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
		dlclose(pHandle);
	pHandle = nullptr;
}
