#include "StrategyExport.h"

extern "C" SXE_EXPORT int SXE_ApiVersion() { return SXE_API_VERSION - 1; }
extern "C" SXE_EXPORT CStrategyBase* SXE_Create() { return nullptr; }
extern "C" SXE_EXPORT void SXE_Destroy(CStrategyBase*) {}
