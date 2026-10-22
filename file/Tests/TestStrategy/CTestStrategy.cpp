#include "Settings.h"
#include "StrategyExport.h"

class CTestStrategy : public CStrategyBase
{
public:
	StrategyInfo GetInfo() const override
	{
		return StrategyInfo{"TestStrategy", "1.0", "test strategy"};
	}

	bool LoadConfig(const std::string& strConfigFile) override
	{
		CReader ini(strConfigFile, false);
		if(!ini.Get("TEST", "tp", dTp) || !ini.Get("TEST", "leverage", dLeverage) ||
		   !ini.Get("TEST", "order_perc", dOrderPerc) || !ini.Get("TEST", "stop_at_step", dStopAtStep))
		{
			strLastError = "missing keys in [TEST]";
			return false;
		}
		return true;
	}

	bool Init(const InitContext&) override { return true; }

	RunResult Run(StepContext& ctx) override
	{
		if(!ctx.position.bOpen)
			ctx.orders.MarketBuy(ctx.account.dAvailable * dOrderPerc / 100.0, dLeverage);
		else if(ctx.dPrice >= ctx.position.dAvgPrice * (1.0 + dTp / 100.0))
			ctx.orders.Close();

		if(dStopAtStep > 0 && ctx.nStep == static_cast<long long>(dStopAtStep))
			return RunResult::Stop;
		return RunResult::Continue;
	}

	void Term() override {}

private:
	double dTp         = 2;
	double dLeverage   = 1;
	double dOrderPerc  = 50;
	double dStopAtStep = 0;
};

SXE_EXPORT_STRATEGY(CTestStrategy)
