#pragma once
#include <string>

#include "StrategyExport.h"

class CStrategyLoader
{
public:
	CStrategyLoader();
	~CStrategyLoader();

	bool Load(const std::string& strPath);
	void Unload();

	CStrategyBase* Get() const { return pStrategy; }
	std::string GetLastError() const { return strLastError; }

private:
	void*          pHandle;
	SXE_DestroyFn  pfnDestroy;
	CStrategyBase* pStrategy;
	std::string    strLastError;
};
