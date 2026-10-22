#pragma once
#include "StrategyBase.h"

#define SXE_SYMBOL_API_VERSION		"SXE_ApiVersion"
#define SXE_SYMBOL_CREATE			"SXE_Create"
#define SXE_SYMBOL_DESTROY			"SXE_Destroy"

typedef int            (*SXE_ApiVersionFn)();
typedef CStrategyBase* (*SXE_CreateFn)();
typedef void           (*SXE_DestroyFn)(CStrategyBase*);

#define SXE_EXPORT __attribute__((visibility("default")))

#define SXE_EXPORT_STRATEGY(ClassName)											\
	extern "C" SXE_EXPORT int SXE_ApiVersion()									\
	{																			\
		return SXE_API_VERSION;													\
	}																			\
	extern "C" SXE_EXPORT CStrategyBase* SXE_Create()							\
	{																			\
		try { return new ClassName(); }											\
		catch(...) { return nullptr; }											\
	}																			\
	extern "C" SXE_EXPORT void SXE_Destroy(CStrategyBase* pStrategy)			\
	{																			\
		delete pStrategy;														\
	}
