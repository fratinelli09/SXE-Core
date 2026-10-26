#pragma once
#include <chrono>
#include <memory>
#include <string>

#include "CCsvFeed.h"
#include "CCycleArchive.h"
#include "CSimExchange.h"
#include "CSimStateFile.h"
#include "CStrategyLoader.h"
#include "Settings.h"
#include "ThreadBase.h"

struct SimResult
{
	bool        bFinished           = false;
	bool        bError              = false;
	std::string strReason;
	long long   nFirstTime          = 0;
	long long   nLastTime           = 0;
	double      dInitialCapital     = 0;
	double      dFinalEquity        = 0;
	double      dReturn             = 0;
	double      dMaxDrawdown        = 0;
	long long   nCycles             = 0;
	long long   nWins               = 0;
	long long   nLiquidations       = 0;
	long long   nOrders             = 0;
	long long   nRejected           = 0;
	double      dFees               = 0;
	double      dFunding            = 0;
	double      dWorstCycleDrawdown = 0;
	double      dMaxLeverage        = 0;
	bool        bOpenAtEnd          = false;
	long long   nUnderwater         = 0;
	long long   nSteps              = 0;
	double      dSeconds            = 0;
};

struct EngineOptions
{
	bool bArchive   = true;
	bool bStateFile = true;
	bool bOrderLog  = true;
};

class CEngine : public ThreadBase
{
public:
	explicit CEngine(Logger& logger = Logger::Get());
	~CEngine() override;

	bool Setup(const CSettings& settings, const EngineOptions& options = EngineOptions());

	void run() override;

	int ExitCode() const { return nExitCode; }

	const SimResult& GetResult() const { return result; }

	void RequestStop() { bStopRequested = true; }

protected:
	Logger& _log() override { return logger; }

private:
	class COrderRouter : public IOrders
	{
	public:
		explicit COrderRouter(CEngine& engine): engine(engine) {}
		OrderResult MarketBuy(double dUsdt, double dLeverage) override;
		OrderResult Close() override;
		OrderResult LimitBuy(double dPrice, double dUsdt, double dLeverage) override;
		OrderResult LimitClose(double dPrice) override;
		OrderResult Move(long long nOrderId, double dPrice) override;
		bool Cancel(long long nOrderId) override;
		void CancelAll() override;
	private:
		CEngine& engine;
	};

	bool _Resume(const CSettings& settings);
	void _SaveState();
	void _OnLiquidation();
	void _OnFill(const Fill& fill);
	void _Finish(bool bError, const std::string& strReason);
	void _ArchiveCycle();
	void _ArchiveOpenCycle();
	void _UpdateContext();
	void _PrintStatus(bool bForce);

private:
	Logger&                       logger;
	CStrategyLoader               loader;
	CStrategyBase*                pStrategy;
	std::unique_ptr<CCsvFeed>     pFeed;
	std::unique_ptr<CSimExchange> pExchange;
	CCycleArchive                 archive;
	COrderRouter                  orderRouter;
	StepContext                   ctx;
	MarketStep                    step;

	bool              bConsole;
	double            dInitialCapital;
	double            dMinNotional;
	std::string       strStrategyName;
	std::string       strDataFile;
	std::string       strStateFile;
	int               nSaveEvery;
	std::atomic<bool> bStopRequested;
	bool              bStrategyStarted;
	bool              bArchiveOpenCycle;
	bool              bSaveNow;
	bool              bStateFile;
	bool              bResumed;
	bool              bLiquidation;
	bool              bUnderwater;
	long long         nUnderwater;
	long long         nOrdersVersion;
	int               nExitCode;
	SimResult         result;

	long long         nStartStep;
	double            dPeakEquity;
	double            dMaxDrawdown;
	std::chrono::steady_clock::time_point tStart;

	struct PendingClose
	{
		bool        bPending = false;
		double      dExitPrice = 0;
		double      dFee = 0;
		Position    positionBefore;
		std::string strReason;
	};
	PendingClose      pendingClose;
};
