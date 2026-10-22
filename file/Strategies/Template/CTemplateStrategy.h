#pragma once
#include <string>
#include <vector>

#include "StrategyBase.h"

class CTemplateStrategy : public CStrategyBase
{
public:
	CTemplateStrategy();
	~CTemplateStrategy() override;

	StrategyInfo GetInfo() const override;
	bool LoadConfig(const std::string& strConfigFile) override;
	bool Init(const InitContext& ctx) override;
	RunResult Run(StepContext& ctx) override;
	void Term() override;

private:
	void _AddClose(double dClose);
	void _WriteCycleFile(const std::string& strLine);

private:
	int    nFastPeriod;
	int    nSlowPeriod;
	double dOrderPerc;
	double dLeverage;

	std::vector<double> vCloses;
	long long nCount;
	double    dSumFast;
	double    dSumSlow;
	bool      bFastAbove;
	bool      bHasPrevious;

	Logger*       pLog;
	std::string   strCycleFile;
	double        dWalletAtOpen;
	long long     nTrades;
	long long     nWins;
	double        dLastWallet;
};
