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

class CEngine : public ThreadBase
{
public:
	CEngine();
	~CEngine() override;

	bool Setup(const CSettings& settings);

	void run() override;

	int ExitCode() const { return nExitCode; }

	void RequestStop() { bStopRequested = true; }

private:
	class COrderRouter : public IOrders
	{
	public:
		explicit COrderRouter(CEngine& engine): engine(engine) {}
		OrderResult MarketBuy(double dUsdt, double dLeverage) override;
		OrderResult Close() override;
	private:
		CEngine& engine;
	};

	bool _Resume(const CSettings& settings);
	void _SaveState();
	void _OnLiquidation();
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
	bool              bResumed;
	bool              bLiquidation;
	bool              bUnderwater;
	long long         nUnderwater;
	int               nExitCode;

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
