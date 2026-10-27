#pragma once
#include <string>

#include "StrategyBase.h"

class CSimpleDcaStrategy : public CStrategyBase
{
public:
	CSimpleDcaStrategy();
	~CSimpleDcaStrategy() override;

	StrategyInfo GetInfo() const override;
	bool LoadConfig(const std::string& strConfigFile) override;
	bool Init(const InitContext& ctx) override;
	RunResult Run(StepContext& ctx) override;
	void Term() override;

private:
	void _Open(StepContext& ctx);
	void _AddSafety(StepContext& ctx);
	void _TakeProfit(StepContext& ctx);

private:
	double dBudgetPerc;
	int    nSafetyOrders;
	double dStepPerc;
	double dSizeMult;
	double dTakeProfitPerc;
	double dLeverage;

	double dWeights;		// 1 + m + m^2 + ... + m^nSafetyOrders: budget = base order x dWeights
	double dBaseMargin;		// margin of the first order of the cycle in progress
	double dEntryPrice;		// fill price of the first order: the safety levels start from here
	int    nFilled;			// orders filled in the cycle in progress (0 = no position)

	Logger*   pLog;
	long long nCycles;
	long long nSafetyFilled;
	long long nLiquidations;
	double    dLastWallet;
};
