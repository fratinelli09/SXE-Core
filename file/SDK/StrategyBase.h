#pragma once
#include <string>

#include "Context.h"

#define SXE_API_VERSION 1

enum class RunResult
{
	Continue,
	Stop,
	Error
};

class CStrategyBase
{
public:
	virtual ~CStrategyBase(){}

	virtual StrategyInfo GetInfo() const = 0;
	virtual bool LoadConfig(const std::string& strConfigFile) = 0;
	virtual bool Init(const InitContext& ctx) = 0;
	virtual RunResult Run(StepContext& ctx) = 0;
	virtual void Term() = 0;

	std::string GetLastError() const { return strLastError; }

protected:
	std::string strLastError;
};
