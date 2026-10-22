#include "CTemplateStrategy.h"

#include <fstream>
#include <locale>
#include <sstream>

#include "Settings.h"
#include "StrategyExport.h"
#include "TimeUtils.h"

#define TEMPLATE_SECTION		"TEMPLATE"
#define TEMPLATE_FAST_PERIOD	"fast_period"
#define TEMPLATE_SLOW_PERIOD	"slow_period"
#define TEMPLATE_ORDER_PERC		"order_perc"
#define TEMPLATE_LEVERAGE		"leverage"
#define TEMPLATE_CYCLE_FILE		"Template.txt"

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

CTemplateStrategy::CTemplateStrategy():
	nFastPeriod(20),
	nSlowPeriod(50),
	dOrderPerc(50),
	dLeverage(1),
	nCount(0),
	dSumFast(0),
	dSumSlow(0),
	bFastAbove(false),
	bHasPrevious(false),
	pLog(nullptr),
	dWalletAtOpen(0),
	nTrades(0),
	nWins(0),
	dLastWallet(0)
{
}

CTemplateStrategy::~CTemplateStrategy()
{
}

StrategyInfo CTemplateStrategy::GetInfo() const
{
	return StrategyInfo{"Template", "1.0", "example: moving-average crossover"};
}

bool CTemplateStrategy::LoadConfig(const std::string& strConfigFile)
{
	CReader ini(strConfigFile, false);
	if(!ini.IsValid())
	{
		strLastError = "file not readable or with invalid lines: " + strConfigFile;
		return false;
	}

	bool bOk = ini.Get(TEMPLATE_SECTION, TEMPLATE_FAST_PERIOD, nFastPeriod);
	bOk = ini.Get(TEMPLATE_SECTION, TEMPLATE_SLOW_PERIOD, nSlowPeriod) && bOk;
	bOk = ini.Get(TEMPLATE_SECTION, TEMPLATE_ORDER_PERC, dOrderPerc) && bOk;
	bOk = ini.Get(TEMPLATE_SECTION, TEMPLATE_LEVERAGE, dLeverage) && bOk;
	if(!bOk)
	{
		strLastError = "missing or invalid keys in [" TEMPLATE_SECTION "]";
		return false;
	}

	if(nFastPeriod < 1 || nSlowPeriod <= nFastPeriod)
	{
		strLastError = "required: " TEMPLATE_FAST_PERIOD " >= 1 and " TEMPLATE_SLOW_PERIOD " > " TEMPLATE_FAST_PERIOD;
		return false;
	}
	if(dOrderPerc <= 0 || dOrderPerc > 100 || dLeverage < 1)
	{
		strLastError = "required: 0 < " TEMPLATE_ORDER_PERC " <= 100 and " TEMPLATE_LEVERAGE " >= 1";
		return false;
	}
	return true;
}

bool CTemplateStrategy::Init(const InitContext& ctx)
{
	pLog = &ctx.log;

	vCloses.assign(nSlowPeriod, 0.0);
	nCount = 0;
	dSumFast = dSumSlow = 0;
	bHasPrevious = false;
	nTrades = nWins = 0;
	dLastWallet = ctx.account.dWallet;

	strCycleFile = ctx.strLogDir + "/" TEMPLATE_CYCLE_FILE;

	pLog->Info("Template: averages " + std::to_string(nFastPeriod) + "/" + std::to_string(nSlowPeriod) +
	           ", order " + Num(dOrderPerc) + "% of the available balance, leverage " + Num(dLeverage) +
	           ", " + ctx.strSymbol + " from " + TimeUtils::Format(ctx.nStartTime));

	if(ctx.position.bOpen)
		pLog->Info("Template: position already open (" + Num(ctx.position.dQty, 4) + " at " + Num(ctx.position.dAvgPrice) +
		           "), averages recomputed from scratch");
	dWalletAtOpen = ctx.account.dWallet;
	return true;
}

void CTemplateStrategy::_AddClose(double dClose)
{
	const size_t nCap = vCloses.size();
	const size_t nPos = static_cast<size_t>(nCount % nCap);

	if(nCount >= static_cast<long long>(nCap))
		dSumSlow -= vCloses[nPos];
	vCloses[nPos] = dClose;
	dSumSlow += dClose;

	dSumFast += dClose;
	if(nCount >= nFastPeriod)
		dSumFast -= vCloses[static_cast<size_t>((nCount - nFastPeriod) % nCap)];

	++nCount;
}

RunResult CTemplateStrategy::Run(StepContext& ctx)
{
	dLastWallet = ctx.account.dWallet;

	if(ctx.bLiquidated)
		pLog->Warning("Template: position liquidated at " + TimeUtils::Format(ctx.nTime));

	if(!ctx.bCandleClosed)
		return RunResult::Continue;

	_AddClose(ctx.lastCandle.dClose);
	if(nCount < nSlowPeriod)
		return RunResult::Continue;

	const double dFast = dSumFast / nFastPeriod;
	const double dSlow = dSumSlow / nSlowPeriod;
	const bool bAbove  = dFast > dSlow;

	const bool bCrossUp   = bHasPrevious && bAbove && !bFastAbove;
	const bool bCrossDown = bHasPrevious && !bAbove && bFastAbove;
	bFastAbove   = bAbove;
	bHasPrevious = true;

	if(bCrossUp && !ctx.position.bOpen)
	{
		const double dUsdt = ctx.account.dAvailable * dOrderPerc / 100.0;
		const double dWallet = ctx.account.dWallet;
		const OrderResult r = ctx.orders.MarketBuy(dUsdt, dLeverage);
		if(r.bOk)
		{
			dWalletAtOpen = dWallet;
			const std::string strLine = "OPENED " + Num(r.dQty, 4) + " at " + Num(r.dFillPrice) + " (" + Num(dUsdt) +
			                            " USDT, averages " + Num(dFast) + " > " + Num(dSlow) + ")";
			pLog->Info("Template: " + strLine);
			_WriteCycleFile("[" + TimeUtils::Format(ctx.nTime) + "] cycle " + std::to_string(ctx.nCycle) + " " + strLine);
			pLog->ClearOnce();
		}
		else
		{
			pLog->WarningOnce("Template: opening rejected (" + r.strError + ")");
		}
	}
	else if(bCrossDown && ctx.position.bOpen)
	{
		const OrderResult r = ctx.orders.Close();
		if(r.bOk)
		{
			const double dPnL = ctx.account.dWallet - dWalletAtOpen;
			++nTrades;
			if(dPnL > 0)
				++nWins;

			const std::string strLine = "CLOSED at " + Num(r.dFillPrice) + " (averages " + Num(dFast) + " < " + Num(dSlow) +
			                            "), result " + Num(dPnL) + " USDT, balance " + Num(ctx.account.dWallet);
			pLog->Info("Template: " + strLine);
			_WriteCycleFile("[" + TimeUtils::Format(ctx.nTime) + "] cycle " + std::to_string(ctx.nCycle) + " " + strLine);
		}
		else
		{
			pLog->WarningOnce("Template: closing rejected (" + r.strError + ")");
		}
	}

	dLastWallet = ctx.account.dWallet;
	return RunResult::Continue;
}

void CTemplateStrategy::_WriteCycleFile(const std::string& strLine)
{
	std::ofstream file(strCycleFile, std::ios::app);
	file << strLine << "\n";
}

void CTemplateStrategy::Term()
{
	if(pLog)
		pLog->Info("Template: " + std::to_string(nTrades) + " trades closed, " + std::to_string(nWins) +
		           " in profit, final balance " + Num(dLastWallet) + " USDT");
}

SXE_EXPORT_STRATEGY(CTemplateStrategy)
