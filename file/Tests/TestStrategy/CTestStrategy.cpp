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
		ini.Get("TEST", "limit_dip", dLimitDip);
		return true;
	}

	bool Init(const InitContext&) override { return true; }

	RunResult Run(StepContext& ctx) override
	{
		if(dLimitDip > 0)
			_RunLimit(ctx);
		else if(!ctx.position.bOpen)
			ctx.orders.MarketBuy(ctx.account.dAvailable * dOrderPerc / 100.0, dLeverage);
		else if(ctx.dPrice >= ctx.position.dAvgPrice * (1.0 + dTp / 100.0))
			ctx.orders.Close();

		if(dStopAtStep > 0 && ctx.nStep == static_cast<long long>(dStopAtStep))
			return RunResult::Stop;
		return RunResult::Continue;
	}

	void Term() override {}

private:
	void _RunLimit(StepContext& ctx)
	{
		if(!ctx.position.bOpen)
		{
			if(!ctx.vOpenOrders.empty())
				ctx.orders.CancelAll();
			if(!ctx.orders.MarketBuy(ctx.account.dAvailable * dOrderPerc / 100.0, dLeverage).bOk)
				return;
			ctx.orders.LimitBuy(ctx.position.dAvgPrice * (1.0 - dLimitDip / 100.0), ctx.account.dAvailable * dOrderPerc / 100.0, dLeverage);
			ctx.orders.LimitClose(ctx.position.dAvgPrice * (1.0 + dTp / 100.0));
			return;
		}
		bool bBought = false;
		for(const Fill& fill : ctx.vFills)
			bBought = bBought || fill.eSide == OrderSide::Buy;
		long long nClose = 0;
		for(const LimitOrder& order : ctx.vOpenOrders)
			if(order.eSide == OrderSide::Close)
				nClose = order.nId;
		if(bBought && nClose > 0)
			ctx.orders.Move(nClose, ctx.position.dAvgPrice * (1.0 + dTp / 100.0));
	}

	double dTp         = 2;
	double dLeverage   = 1;
	double dOrderPerc  = 50;
	double dStopAtStep = 0;
	double dLimitDip   = 0;
};

SXE_EXPORT_STRATEGY(CTestStrategy)
