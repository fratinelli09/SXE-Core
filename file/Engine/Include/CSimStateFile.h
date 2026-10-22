#pragma once
#include <string>

#include "CCycleArchive.h"
#include "CSimExchange.h"

struct SimState
{
	std::string strDataFile;
	std::string strStrategy;
	long long   nCandleTime   = -1;
	long long   nNextTick     = 0;
	long long   nStep         = 0;
	double      dPeakEquity   = 0;
	double      dMaxDrawdown  = 0;

	CSimExchange::Snapshot  exchange;
	CCycleArchive::Snapshot cycle;
};

class CSimStateFile
{
public:
	static bool Save(const std::string& strFile, const SimState& state, std::string& strError);
	static bool Load(const std::string& strFile, SimState& state, std::string& strError);
};
