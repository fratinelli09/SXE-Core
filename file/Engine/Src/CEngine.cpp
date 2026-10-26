#include "CEngine.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <system_error>

#include "TimeUtils.h"

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

	const long long STATUS_EVERY_STEPS = 5000;
}

CEngine::CEngine(Logger& log):
	logger(log),
	pStrategy(nullptr),
	orderRouter(*this),
	ctx{0, 0, 0, 0, Candle(), false, Account(), Position(), false, {}, {}, orderRouter, log},
	step(),
	bConsole(false),
	dInitialCapital(0),
	dMinNotional(0),
	nSaveEvery(500),
	bStopRequested(false),
	bStrategyStarted(false),
	bArchiveOpenCycle(false),
	bSaveNow(false),
	bStateFile(true),
	nProgressEvery(0),
	fnProgress(),
	bResumed(false),
	bLiquidation(true),
	bUnderwater(false),
	nUnderwater(0),
	nOrdersVersion(-1),
	nExitCode(0),
	nStartStep(0),
	dPeakEquity(0),
	dMaxDrawdown(0)
{
	ThreadBase::_bInitFailed = true;
}

CEngine::~CEngine()
{
	stop();
	wait();
}

bool CEngine::Setup(const CSettings& settings, const EngineOptions& options)
{
	bConsole   = settings.log.nConsole != 0;
	bStateFile = options.bStateFile;
	nProgressEvery = options.nProgressEvery;
	fnProgress     = options.fnProgress;
	archive.SetCycleCallback(options.fnCycle);

	if(!settings.bSimulate)
	{
		logger.Error("Mode " CFG_MODE_REAL " is not available in this version of the engine");
		return false;
	}

	if(!loader.Load(settings.strategy.LibraryPath()))
	{
		logger.Error(loader.GetLastError());
		return false;
	}
	pStrategy = loader.Get();

	const StrategyInfo info = pStrategy->GetInfo();
	strStrategyName = info.strName.empty() ? "Strategy" : info.strName;
	logger.Info("Strategy loaded: " + strStrategyName + " " + info.strVersion +
	            (info.strDescription.empty() ? "" : " - " + info.strDescription));

	if(!pStrategy->LoadConfig(settings.strategy.strConfig))
	{
		logger.Error("LoadConfig(" + settings.strategy.strConfig + ") failed: " + pStrategy->GetLastError());
		return false;
	}

	strDataFile = settings.sim.strDataFile;
	pFeed = std::make_unique<CCsvFeed>();
	if(!pFeed->Load(strDataFile, settings.sim.strStartDate, settings.sim.strEndDate))
	{
		logger.Error(pFeed->GetLastError());
		return false;
	}
	pFeed->SetTicks(settings.sim.nIntrabar, settings.sim.dTickStep);

	static const char* INTRABAR[] = { "close only", "by candle direction", "low first", "high first" };
	logger.Info("Candles loaded: " + std::to_string(pFeed->Count()) + " of " + std::to_string(pFeed->Interval() / 60) +
	            " minutes (" + TimeUtils::Format(pFeed->FirstTime()) + " -> " + TimeUtils::Format(pFeed->LastTime()) +
	            "), path " + INTRABAR[settings.sim.nIntrabar] + ", tick every " + Num(settings.sim.dTickStep * 100, 2) + "%");
	if(pFeed->SkippedRows() > 0 || pFeed->Gaps() > 0)
		logger.Warning("Incomplete data in " + strDataFile + ": " + std::to_string(pFeed->SkippedRows()) +
		               " invalid rows skipped, " + std::to_string(pFeed->Gaps()) +
		               " gaps between consecutive candles: the simulation skips those periods");

	const std::string strFrom = TimeUtils::Format(pFeed->FirstTime());
	const std::string strTo   = TimeUtils::Format(pFeed->LastTime() + pFeed->Interval());
	const std::string strAsked = (settings.sim.strStartDate.empty() ? std::string("start of data") : settings.sim.strStartDate) +
	                             " -> " + (settings.sim.strEndDate.empty() ? std::string("end of data") : settings.sim.strEndDate);
	logger.Info("Requested period: " + strAsked + "; simulated: " + strFrom + " -> " + strTo);
	const long long nAskedStart = settings.sim.strStartDate.empty() ? 0 : TimeUtils::Parse(settings.sim.strStartDate);
	const long long nAskedEnd   = settings.sim.strEndDate.empty() ? 0 : TimeUtils::Parse(settings.sim.strEndDate) + 24 * 3600;
	if((nAskedStart > 0 && pFeed->FirstTime() > nAskedStart + pFeed->Interval()) ||
	   (nAskedEnd > 0 && pFeed->LastTime() + 2 * pFeed->Interval() < nAskedEnd))
		logger.Warning("The data do not cover the whole requested period (" + strAsked + "): simulating only " +
		               strFrom + " -> " + strTo);
	if(bConsole)
		std::cout << "SXE: " << strStrategyName << " from " << strFrom << " to " << strTo << " ("
		          << pFeed->Count() << " candles of " << pFeed->Interval() / 60 << " minutes)" << std::endl;

	ExchangeRules rules;
	rules.eMarginMode        = (settings.exchange.strMarginMode == CFG_MARGIN_CROSSED) ? MarginMode::Crossed : MarginMode::Isolated;
	rules.dFeeTaker          = settings.exchange.dFeeTaker;
	rules.dFeeMaker          = settings.exchange.dFeeMaker;
	rules.dSlippage          = settings.exchange.dSlippage;
	rules.dMaintenanceMargin = settings.exchange.dMaintenanceMargin;
	rules.dQtyStep           = settings.exchange.dQtyStep;
	rules.dMinQty            = settings.exchange.dMinQty;
	rules.dMinNotional       = settings.exchange.dMinNotional;

	dInitialCapital   = settings.sim.dInitialCapital;
	dMinNotional      = settings.exchange.dMinNotional;
	strStateFile      = settings.sim.strStateFile;
	nSaveEvery        = settings.sim.nSaveEvery;
	bLiquidation      = settings.sim.nLiquidation != 0;
	bArchiveOpenCycle = settings.sim.nResume == 0;

	pExchange = std::make_unique<CSimExchange>(rules, dInitialCapital);
	pExchange->SetSimRules(settings.sim.nLiquidation != 0, settings.sim.dFundingRate, settings.sim.dFundingMultiplier);
	pExchange->SetMaintenanceTiers(settings.exchange.vMaintenanceTiers);
	pExchange->SetLimitRules(settings.sim.strLimitFill == CFG_LIMIT_FILL_THROUGH, settings.sim.dTickStep);

	std::string strError;
	if(!pExchange->LoadFunding(settings.sim.strFundingFile, strError))
	{
		logger.Error(strError);
		return false;
	}
	logger.Info("Simulated exchange: fee taker " + Num(settings.exchange.dFeeTaker * 100, 4) + "% (market orders), maker " +
	            Num(settings.exchange.dFeeMaker * 100, 4) + "% (limit orders, filled " +
	            (settings.sim.strLimitFill == CFG_LIMIT_FILL_THROUGH
	                 ? "when the price goes through the limit by " + Num(settings.sim.dTickStep * 100, 2) + "%"
	                 : std::string("when the price touches the limit")) + ")");
	logger.Info("Simulated exchange: margin " + settings.exchange.strMarginMode + ", liquidation " +
	            (settings.sim.nLiquidation ? "on" : "off") + ", funding " +
	            (pExchange->FundingCount() ? "from history (" + std::to_string(pExchange->FundingCount()) + " values), elsewhere " : std::string()) +
	            Num(settings.sim.dFundingRate * 100, 4) + "% every 8 hours" +
	            (settings.sim.dFundingMultiplier != 1 ? ", funding multiplied x" + Num(settings.sim.dFundingMultiplier, 2) : std::string()) +
	            ", slippage " + Num(settings.exchange.dSlippage * 100, 4) + "%, maintenance margin " +
	            std::to_string(settings.exchange.vMaintenanceTiers.size()) + " tiers (" +
	            Num(settings.exchange.dMaintenanceMargin * 100, 2) + "% up to " +
	            Num(settings.exchange.vMaintenanceTiers.empty() ? 0 : settings.exchange.vMaintenanceTiers.front().first, 0) + " USDT)");

	if(options.bOrderLog && !pExchange->SetOrderLog(settings.sim.strOrderLog, settings.sim.nResume != 0))
	{
		logger.Error("Order log cannot be written: " + settings.sim.strOrderLog);
		return false;
	}

	if(!archive.Setup(settings.path.strLog, settings.path.strBackup, {strStateFile}, strError, options.bArchive))
	{
		logger.Error(strError);
		return false;
	}

	if(settings.sim.nResume && !_Resume(settings))
		return false;

	ctx.nCycle = archive.CurrentCycle();
	nStartStep = ctx.nStep;
	if(dPeakEquity <= 0)
		dPeakEquity = pExchange->GetAccount().dEquity;

	const long long nNow = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	const long long nFrom = pFeed->CurrentCandleTime() >= 0 ? pFeed->CurrentCandleTime() : pFeed->FirstTime();
	archive.WriteNote(TimeUtils::Format(nNow) + (nStartStep > 0 ? " resume " : " start ") + CFG_MODE_SIM ": " +
	                  strStrategyName + " " + info.strVersion + ", " + settings.strSymbol + ", data " +
	                  std::filesystem::path(strDataFile).filename().string() + " " + TimeUtils::Format(nFrom) + " -> " +
	                  TimeUtils::Format(pFeed->LastTime()) + ", capital " + Num(pExchange->GetAccount().dEquity) +
	                  ", first cycle b" + std::to_string(archive.CurrentCycle()));

	InitContext initCtx{Mode::Simulated, bResumed, settings.strSymbol, nFrom, settings.path.strLog, settings.path.strBackup,
	                    pExchange->GetAccount(), pExchange->GetPosition(), pExchange->GetRules(), logger};
	if(!pStrategy->Init(initCtx))
	{
		logger.Error("Strategy Init failed: " + pStrategy->GetLastError());
		return false;
	}
	bStrategyStarted = true;

	logger.Info("Simulation ready: equity " + Num(pExchange->GetAccount().dEquity) + " USDT" +
	            (pExchange->GetPosition().bOpen ? ", position open" : ""));
	tStart = std::chrono::steady_clock::now();
	ThreadBase::_bInitFailed = false;
	return true;
}

bool CEngine::_Resume(const CSettings& settings)
{
	std::error_code ec;
	if(!std::filesystem::exists(strStateFile, ec))
	{
		logger.Warning("Resume requested but " + strStateFile + " does not exist: the simulation starts from scratch");
		return true;
	}

	SimState state;
	std::string strError;
	if(!CSimStateFile::Load(strStateFile, state, strError))
	{
		logger.Error(strError + " (set " CFG_SIM_RESUME " = 0 to start from scratch)");
		return false;
	}

	if(state.strDataFile != settings.sim.strDataFile)
	{
		logger.Error("The state in " + strStateFile + " was saved with " + state.strDataFile + ", now the data are " +
		             settings.sim.strDataFile + " (set " CFG_SIM_RESUME " = 0 to start from scratch)");
		return false;
	}
	if(state.strStrategy != strStrategyName)
		logger.Warning("The state in " + strStateFile + " was saved with the strategy " + state.strStrategy +
		               ", now the strategy is " + strStrategyName);

	if(state.nCandleTime >= 0 && !pFeed->Seek(state.nCandleTime, static_cast<size_t>(state.nNextTick)))
	{
		logger.Error("Resume impossible: the candle " + TimeUtils::Format(state.nCandleTime) +
		             " of the state is not in the data period (" CFG_SIM_START_DATE ", " CFG_SIM_END_DATE ")");
		return false;
	}

	pExchange->Restore(state.exchange);
	archive.Restore(state.cycle);
	bResumed     = true;
	ctx.nStep    = state.nStep;
	ctx.nTime    = state.exchange.nTime;
	ctx.dPrice   = state.exchange.dPrice;
	logger.SetEngineTime(ctx.nTime);
	dPeakEquity  = state.dPeakEquity;
	dMaxDrawdown = state.dMaxDrawdown;

	logger.Info("Resuming from " + strStateFile + ": candle " +
	            (state.nCandleTime >= 0 ? TimeUtils::Format(state.nCandleTime) : std::string("initial")) +
	            ", step " + std::to_string(state.nStep) + ", balance " + Num(state.exchange.dWallet) +
	            (state.exchange.position.bOpen ? ", position open: " + Num(state.exchange.position.dQty, 6) +
	                                             " at average price " + Num(state.exchange.position.dAvgPrice)
	                                           : std::string(", no position")));
	return true;
}

void CEngine::_SaveState()
{
	if(!bStateFile)
		return;

	SimState state;
	state.strDataFile  = strDataFile;
	state.strStrategy  = strStrategyName;
	state.nCandleTime  = pFeed->CurrentCandleTime();
	state.nNextTick    = static_cast<long long>(pFeed->NextTick());
	state.nStep        = ctx.nStep;
	state.dPeakEquity  = dPeakEquity;
	state.dMaxDrawdown = dMaxDrawdown;
	state.exchange     = pExchange->GetSnapshot();
	state.cycle        = archive.GetSnapshot();

	std::string strError;
	if(!CSimStateFile::Save(strStateFile, state, strError))
		logger.ErrorOnce(strError);
}

void CEngine::run()
{
	if(bStopRequested)
	{
		_Finish(false, "stop requested");
		return;
	}

	if(!pFeed->Next(step))
	{
		_Finish(false, "end of data");
		return;
	}

	logger.SetEngineTime(step.nTime);

	if(step.bFirstTick)
		pExchange->ApplyFunding(step.nCandleTime, step.dPrice);

	pExchange->Refresh(step.nTime, step.dPrice);
	ctx.vFills = pExchange->TakeFills();
	for(const Fill& fill : ctx.vFills)
		_OnFill(fill);

	ctx.nStep++;
	ctx.nTime         = step.nTime;
	ctx.dPrice        = step.dPrice;
	ctx.lastCandle    = step.lastCandle;
	ctx.bCandleClosed = step.bCandleClosed;
	ctx.bLiquidated   = pExchange->TakeLiquidated();
	if(ctx.bLiquidated)
		_OnLiquidation();
	_UpdateContext();
	archive.OnStep(ctx.account, ctx.position);

	if(!bLiquidation && !bUnderwater && ctx.position.bOpen && ctx.account.dEquity < 0)
	{
		bUnderwater = true;
		++nUnderwater;
		logger.Warning("Equity below zero (" + Num(ctx.account.dEquity) + " USDT) in cycle " + std::to_string(ctx.nCycle) +
		               " with " CFG_SIM_LIQUIDATION " = 0: on a real account the position would already have been "
		               "liquidated, the result of the simulation is not realistic");
	}

	const RunResult result = pStrategy->Run(ctx);

	if(pendingClose.bPending)
		_ArchiveCycle();

	const double dEquity = pExchange->GetAccount().dEquity;
	if(dEquity > dPeakEquity)
		dPeakEquity = dEquity;
	else if(dPeakEquity > 0 && (dEquity / dPeakEquity - 1.0) * 100.0 < dMaxDrawdown)
		dMaxDrawdown = (dEquity / dPeakEquity - 1.0) * 100.0;

	if(fnProgress && nProgressEvery > 0 && ctx.nStep % nProgressEvery == 0)
	{
		ProgressInfo progress;
		progress.nTime     = ctx.nTime;
		progress.nStep     = ctx.nStep;
		progress.nCycle    = ctx.nCycle;
		progress.dPrice    = ctx.dPrice;
		progress.dEquity   = dEquity;
		progress.dDrawdown = (dPeakEquity > 0) ? (dEquity / dPeakEquity - 1.0) * 100.0 : 0;
		fnProgress(progress);
	}

	if(bSaveNow || (step.bCandleClosed && pFeed->CandlesDone() % static_cast<size_t>(nSaveEvery) == 0))
	{
		_SaveState();
		bSaveNow = false;
	}

	if(result == RunResult::Error)
	{
		_Finish(true, "strategy error: " + pStrategy->GetLastError());
		return;
	}
	if(result == RunResult::Stop)
	{
		_Finish(false, "the strategy asked to stop");
		return;
	}
	if(!pExchange->GetPosition().bOpen && pExchange->GetAccount().dWallet < dMinNotional)
	{
		_Finish(false, "capital " + Num(pExchange->GetAccount().dWallet) + " USDT below the minimum order");
		return;
	}

	if(bConsole && ctx.nStep % STATUS_EVERY_STEPS == 0)
		_PrintStatus(false);
}

void CEngine::_OnLiquidation()
{
	const CSimExchange::Liquidation& liq = pExchange->GetLastLiquidation();
	const Position& p = liq.positionBefore;

	logger.Error("LIQUIDATION at " + Num(liq.dPrice) + ": position of " + Num(p.dQty, 6) + " at average price " +
	             Num(p.dAvgPrice) + " (liquidation price " + Num(p.dLiquidationPrice) + "), balance -> " +
	             Num(liq.dWalletAfter) + " USDT");

	pendingClose.bPending       = true;
	pendingClose.dExitPrice     = liq.dPrice;
	pendingClose.dFee           = 0;
	pendingClose.positionBefore = p;
	pendingClose.strReason      = "liquidation";
	bSaveNow = true;
}

void CEngine::_OnFill(const Fill& fill)
{
	if(fill.eSide == OrderSide::Buy)
	{
		if(pendingClose.bPending)
			_ArchiveCycle();
		OrderResult r;
		r.bOk        = true;
		r.dFillPrice = fill.dPrice;
		r.dQty       = fill.dQty;
		r.dFee       = fill.dFee;
		r.eType      = fill.eType;
		r.dFeeRate   = fill.dFeeRate;
		r.nOrderId   = fill.nOrderId;
		archive.OnBuy(fill.nTime, r, fill.dWalletBefore);
	}
	else
	{
		pendingClose.bPending       = true;
		pendingClose.dExitPrice     = fill.dPrice;
		pendingClose.dFee           = fill.dFee;
		pendingClose.positionBefore = pExchange->GetLastClosed();
		pendingClose.strReason      = "limit";
	}
	bSaveNow = true;
}

void CEngine::_ArchiveCycle()
{
	pendingClose.bPending = false;
	bUnderwater = false;

	std::string strError;
	const std::string strSummary = archive.Close(ctx.nTime, pendingClose.dExitPrice, pendingClose.positionBefore,
	                                             pendingClose.dFee, pExchange->GetAccount().dWallet,
	                                             pendingClose.strReason, strError);
	logger.Info(strSummary);

	const long long nClosed = archive.CurrentCycle();
	if(!archive.MoveLogFiles(strError) || !strError.empty())
		logger.Error(strError);
	else
		logger.Info("Backup of cycle " + std::to_string(nClosed) + " saved");

	ctx.nCycle = archive.CurrentCycle();
}

void CEngine::_ArchiveOpenCycle()
{
	const Position& position = pExchange->GetPosition();
	if(!bArchiveOpenCycle || !position.bOpen)
		return;

	std::string strError;
	const std::string strSummary = archive.Close(ctx.nTime, ctx.dPrice, position, 0, pExchange->GetAccount().dEquity,
	                                             "open at end of simulation", strError);
	logger.Info(strSummary);

	const long long nCycle = archive.CurrentCycle();
	if(!archive.MoveLogFiles(strError) || !strError.empty())
		logger.Error(strError);
	else
		logger.Info("Backup of cycle " + std::to_string(nCycle) + " (still open) saved");
}

void CEngine::_UpdateContext()
{
	ctx.account  = pExchange->GetAccount();
	ctx.position = pExchange->GetPosition();
	if(pExchange->OrdersVersion() != nOrdersVersion)
	{
		ctx.vOpenOrders = pExchange->GetOpenOrders();
		nOrdersVersion  = pExchange->OrdersVersion();
	}
}

void CEngine::_PrintStatus(bool bForce)
{
	std::cout << "\r[" << TimeUtils::Format(ctx.nTime) << "] " << Num(pFeed->Progress() * 100, 1) << "%"
	          << " | price " << Num(ctx.dPrice)
	          << " | equity " << Num(ctx.account.dEquity)
	          << " | position " << (ctx.position.bOpen ? "open" : "closed") << "      ";
	if(bForce)
		std::cout << std::endl;
	else
		std::cout << std::flush;
}

void CEngine::_Finish(bool bError, const std::string& strReason)
{
	if(bStrategyStarted)
	{
		pStrategy->Term();
		bStrategyStarted = false;
	}

	if(bConsole)
		_PrintStatus(true);

	_SaveState();
	_ArchiveOpenCycle();

	const CSimExchange::Stats& stats = pExchange->GetStats();
	const Account& account = pExchange->GetAccount();
	const double dReturn = (dInitialCapital > 0) ? (account.dEquity / dInitialCapital - 1.0) * 100.0 : 0;
	const double dSec    = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
	const long long nSteps = ctx.nStep - nStartStep;

	const std::string strSummary =
		"Simulation finished (" + strReason + ") " +
		(ctx.nStep > 0 ? "at " + TimeUtils::Format(ctx.nTime) : std::string("before the first step")) +
		(nSteps == 0 && ctx.nStep > 0 ? " without new steps" : "") +
		": equity " + Num(account.dEquity) + " USDT (" + (dReturn >= 0 ? "+" : "") + Num(dReturn) + "% on the initial capital)" +
		", drawdown max " + Num(dMaxDrawdown) + "%" +
		", closed cycles " + std::to_string(stats.nClosed) + " (in profit " + std::to_string(stats.nWins) + ")" +
		", liquidations " + std::to_string(stats.nLiquidations) +
		", orders " + std::to_string(stats.nOrders) + ", rejected " + std::to_string(stats.nRejected) +
		", fees " + Num(stats.dFeesPaid) + " USDT" +
		(stats.nLimitOrders > 0 ? " (maker " + Num(stats.dMakerFees) + " USDT on " + std::to_string(stats.nLimitFills) +
		                          " limit fills, taker " + Num(stats.dFeesPaid - stats.dMakerFees) + " USDT)" : std::string()) +
		", funding " + Num(stats.dFundingPaid) + " USDT" +
		(nUnderwater > 0 ? ", cycles with equity below zero " + std::to_string(nUnderwater) +
		                   " (" CFG_SIM_LIQUIDATION " = 0: result not realistic)" : std::string()) +
		(pExchange->GetPosition().bOpen ? (bArchiveOpenCycle ? ", last cycle still open (archived)"
		                                                     : ", cycle " + std::to_string(archive.CurrentCycle()) + " still open (kept in Log)")
		                                : "") +
		"; " + std::to_string(nSteps) + " steps in " + Num(dSec, 1) + " s (" +
		Num(dSec > 0 ? nSteps / dSec : 0, 0) + " steps/s)";

	if(bError)
	{
		nExitCode = 1;
		logger.Error(strSummary);
	}
	else
	{
		logger.Info(strSummary);
	}
	if(bConsole)
		std::cout << strSummary << std::endl;
	archive.WriteNote(strSummary);

	const CCycleArchive::Totals& totals = archive.GetTotals();
	result.bFinished           = true;
	result.bError              = bError;
	result.strReason           = strReason;
	result.nFirstTime          = pFeed->FirstTime();
	result.nLastTime           = pFeed->LastTime();
	result.dInitialCapital     = dInitialCapital;
	result.dFinalEquity        = account.dEquity;
	result.dReturn             = dReturn;
	result.dMaxDrawdown        = dMaxDrawdown;
	result.nCycles             = stats.nClosed;
	result.nWins               = stats.nWins;
	result.nLiquidations       = stats.nLiquidations;
	result.nOrders             = stats.nOrders;
	result.nRejected           = stats.nRejected;
	result.dFees               = stats.dFeesPaid;
	result.dFunding            = stats.dFundingPaid;
	result.dWorstCycleDrawdown = totals.dWorstDrawdown;
	result.dMaxLeverage        = totals.dMaxLeverage;
	result.bOpenAtEnd          = pExchange->GetPosition().bOpen;
	result.nUnderwater         = nUnderwater;
	result.nSteps              = nSteps;
	result.dSeconds            = dSec;
	if(archive.IsOpen())
	{
		const CCycleArchive::Snapshot open = archive.GetSnapshot();
		result.dWorstCycleDrawdown = std::min(result.dWorstCycleDrawdown, open.dMinEquityPerc);
		result.dMaxLeverage        = std::max(result.dMaxLeverage, open.dMaxLeverage);
	}

	ThreadBase::stop();
}

OrderResult CEngine::COrderRouter::MarketBuy(double dUsdt, double dLeverage)
{
	if(engine.pendingClose.bPending)
		engine._ArchiveCycle();

	const double dWalletBefore = engine.pExchange->GetAccount().dWallet;
	const OrderResult r = engine.pExchange->MarketBuy(dUsdt, dLeverage);
	if(r.bOk)
	{
		engine.archive.OnBuy(engine.ctx.nTime, r, dWalletBefore);
		engine.bSaveNow = true;
	}
	engine._UpdateContext();
	return r;
}

OrderResult CEngine::COrderRouter::Close()
{
	const Position positionBefore = engine.pExchange->GetPosition();
	const OrderResult r = engine.pExchange->Close();
	if(r.bOk)
	{
		engine.pendingClose.bPending       = true;
		engine.pendingClose.dExitPrice     = r.dFillPrice;
		engine.pendingClose.dFee           = r.dFee;
		engine.pendingClose.positionBefore = positionBefore;
		engine.pendingClose.strReason      = "strategy";
		engine.bSaveNow = true;
	}
	engine._UpdateContext();
	return r;
}

OrderResult CEngine::COrderRouter::LimitBuy(double dPrice, double dUsdt, double dLeverage)
{
	const OrderResult r = engine.pExchange->LimitBuy(dPrice, dUsdt, dLeverage);
	if(r.bOk)
		engine.bSaveNow = true;
	engine._UpdateContext();
	return r;
}

OrderResult CEngine::COrderRouter::LimitClose(double dPrice)
{
	const OrderResult r = engine.pExchange->LimitClose(dPrice);
	if(r.bOk)
		engine.bSaveNow = true;
	engine._UpdateContext();
	return r;
}

OrderResult CEngine::COrderRouter::Move(long long nOrderId, double dPrice)
{
	const OrderResult r = engine.pExchange->Move(nOrderId, dPrice);
	if(r.bOk)
		engine.bSaveNow = true;
	engine._UpdateContext();
	return r;
}

bool CEngine::COrderRouter::Cancel(long long nOrderId)
{
	const bool bOk = engine.pExchange->Cancel(nOrderId);
	if(bOk)
		engine.bSaveNow = true;
	engine._UpdateContext();
	return bOk;
}

void CEngine::COrderRouter::CancelAll()
{
	engine.pExchange->CancelAll();
	engine.bSaveNow = true;
	engine._UpdateContext();
}
