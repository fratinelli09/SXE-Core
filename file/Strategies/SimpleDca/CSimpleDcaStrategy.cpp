#include "CSimpleDcaStrategy.h"

#include <cmath>
#include <locale>
#include <sstream>

#include "Settings.h"
#include "StrategyExport.h"
#include "TimeUtils.h"

#define DCA_SECTION			"SIMPLE_DCA"
#define DCA_BUDGET_PERC		"budget_perc"
#define DCA_SAFETY_ORDERS	"safety_orders"
#define DCA_STEP_PERC		"step_perc"
#define DCA_SIZE_MULT		"size_mult"
#define DCA_TAKE_PROFIT		"take_profit_perc"
#define DCA_LEVERAGE		"leverage"

namespace
{
	std::string Num(double d, int nDec = 2)
	{
		std::ostringstream os;
		os.imbue(std::locale::classic());
		os.setf(std::ios::fixed);
		os.precision(nDec);
		os << d;
		return os.str();
	}
}

CSimpleDcaStrategy::CSimpleDcaStrategy():
	dBudgetPerc(95),
	nSafetyOrders(4),
	dStepPerc(3),
	dSizeMult(1.5),
	dTakeProfitPerc(1.5),
	dLeverage(3),
	dWeights(1),
	dBaseMargin(0),
	dEntryPrice(0),
	nFilled(0),
	pLog(nullptr),
	nCycles(0),
	nSafetyFilled(0),
	nLiquidations(0),
	dLastWallet(0)
{
}

CSimpleDcaStrategy::~CSimpleDcaStrategy()
{
}

StrategyInfo CSimpleDcaStrategy::GetInfo() const
{
	return StrategyInfo{"SimpleDca", "1.0", "example: dollar-cost averaging with safety orders and take profit"};
}

bool CSimpleDcaStrategy::LoadConfig(const std::string& strConfigFile)
{
	CReader ini(strConfigFile, false);
	if(!ini.IsValid())
	{
		strLastError = "file not readable or with invalid lines: " + strConfigFile;
		return false;
	}

	bool bOk = ini.Get(DCA_SECTION, DCA_BUDGET_PERC, dBudgetPerc);
	bOk = ini.Get(DCA_SECTION, DCA_SAFETY_ORDERS, nSafetyOrders) && bOk;
	bOk = ini.Get(DCA_SECTION, DCA_STEP_PERC, dStepPerc) && bOk;
	bOk = ini.Get(DCA_SECTION, DCA_SIZE_MULT, dSizeMult) && bOk;
	bOk = ini.Get(DCA_SECTION, DCA_TAKE_PROFIT, dTakeProfitPerc) && bOk;
	bOk = ini.Get(DCA_SECTION, DCA_LEVERAGE, dLeverage) && bOk;
	if(!bOk)
	{
		strLastError = "missing or invalid keys in [" DCA_SECTION "]";
		return false;
	}

	if(dBudgetPerc <= 0 || dBudgetPerc > 100 || dLeverage < 1)
	{
		strLastError = "required: 0 < " DCA_BUDGET_PERC " <= 100 and " DCA_LEVERAGE " >= 1";
		return false;
	}
	if(nSafetyOrders < 0 || dSizeMult <= 0 || dTakeProfitPerc <= 0)
	{
		strLastError = "required: " DCA_SAFETY_ORDERS " >= 0, " DCA_SIZE_MULT " > 0 and " DCA_TAKE_PROFIT " > 0";
		return false;
	}
	if(dStepPerc <= 0 || dStepPerc * nSafetyOrders >= 100)
	{
		strLastError = "required: " DCA_STEP_PERC " > 0 and " DCA_STEP_PERC " x " DCA_SAFETY_ORDERS " < 100";
		return false;
	}

	dWeights = 0;
	for(int i = 0; i <= nSafetyOrders; i++)
		dWeights += std::pow(dSizeMult, i);
	return true;
}

bool CSimpleDcaStrategy::Init(const InitContext& ctx)
{
	pLog = &ctx.log;
	nCycles = nSafetyFilled = nLiquidations = 0;
	dLastWallet = ctx.account.dWallet;

	pLog->Info("SimpleDca: budget " + Num(dBudgetPerc) + "% of the available balance, " + std::to_string(nSafetyOrders) +
	           " safety orders every " + Num(dStepPerc) + "% (x" + Num(dSizeMult) + "), take profit " +
	           Num(dTakeProfitPerc) + "%, leverage " + Num(dLeverage) + ", " + ctx.strSymbol + " from " +
	           TimeUtils::Format(ctx.nStartTime));

	nFilled = 0;
	if(ctx.position.bOpen)
	{
		// resumed with an open position: the orders already filled are not known, only the take profit is managed
		dEntryPrice = ctx.position.dAvgPrice;
		nFilled     = nSafetyOrders + 1;
		pLog->Info("SimpleDca: position already open (" + Num(ctx.position.dQty, 4) + " at " +
		           Num(ctx.position.dAvgPrice) + "), no further safety orders in this cycle");
	}
	return true;
}

RunResult CSimpleDcaStrategy::Run(StepContext& ctx)
{
	dLastWallet = ctx.account.dWallet;

	if(ctx.bLiquidated)
	{
		++nLiquidations;
		pLog->Warning("SimpleDca: position liquidated at " + TimeUtils::Format(ctx.nTime));
	}
	if(!ctx.position.bOpen)
		nFilled = 0;

	if(nFilled == 0)
		_Open(ctx);
	else
	{
		_AddSafety(ctx);
		_TakeProfit(ctx);
	}

	dLastWallet = ctx.account.dWallet;
	return RunResult::Continue;
}

// a new cycle starts at the close of a candle: base order = budget / dWeights
void CSimpleDcaStrategy::_Open(StepContext& ctx)
{
	if(!ctx.bCandleClosed)
		return;

	const double dBudget = ctx.account.dAvailable * dBudgetPerc / 100.0;
	const double dMargin = dBudget / dWeights;
	const OrderResult r = ctx.orders.MarketBuy(dMargin, dLeverage);
	if(!r.bOk)
	{
		pLog->WarningOnce("SimpleDca: opening rejected (" + r.strError + ")");
		return;
	}
	pLog->ClearOnce();
	++nCycles;
	dBaseMargin = dMargin;
	dEntryPrice = r.dFillPrice;
	nFilled     = 1;
	pLog->Info("SimpleDca: cycle " + std::to_string(ctx.nCycle) + " opened, " + Num(r.dQty, 4) + " at " +
	           Num(r.dFillPrice) + " (" + Num(dMargin) + " USDT of margin, budget " + Num(dBudget) + ")");
}

// safety order k (k = 1..nSafetyOrders) when the price is k x step_perc below the first entry,
// with margin base x size_mult^k
void CSimpleDcaStrategy::_AddSafety(StepContext& ctx)
{
	if(nFilled > nSafetyOrders)
		return;
	const double dLevel = dEntryPrice * (1.0 - nFilled * dStepPerc / 100.0);
	if(ctx.dPrice > dLevel)
		return;

	const double dMargin = dBaseMargin * std::pow(dSizeMult, nFilled);
	const OrderResult r = ctx.orders.MarketBuy(dMargin, dLeverage);
	if(!r.bOk)
	{
		// not retried: the cycle continues with the orders filled so far
		pLog->Warning("SimpleDca: safety order " + std::to_string(nFilled) + " rejected (" + r.strError + ")");
		nFilled = nSafetyOrders + 1;
		return;
	}
	++nSafetyFilled;
	pLog->Info("SimpleDca: safety order " + std::to_string(nFilled) + ", " + Num(r.dQty, 4) + " at " +
	           Num(r.dFillPrice) + ", average price " + Num(ctx.position.dAvgPrice));
	++nFilled;
}

void CSimpleDcaStrategy::_TakeProfit(StepContext& ctx)
{
	if(!ctx.position.bOpen || ctx.dPrice < ctx.position.dAvgPrice * (1.0 + dTakeProfitPerc / 100.0))
		return;

	const OrderResult r = ctx.orders.Close();
	if(!r.bOk)
	{
		pLog->WarningOnce("SimpleDca: take profit rejected (" + r.strError + ")");
		return;
	}
	pLog->Info("SimpleDca: take profit at " + Num(r.dFillPrice) + ", balance " + Num(ctx.account.dWallet));
	nFilled = 0;
}

void CSimpleDcaStrategy::Term()
{
	if(pLog)
		pLog->Info("SimpleDca: " + std::to_string(nCycles) + " cycles opened, " + std::to_string(nSafetyFilled) +
		           " safety orders, " + std::to_string(nLiquidations) + " liquidations, final balance " +
		           Num(dLastWallet) + " USDT");
}

SXE_EXPORT_STRATEGY(CSimpleDcaStrategy)
