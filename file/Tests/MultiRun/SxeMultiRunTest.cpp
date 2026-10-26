#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "CEngine.h"
#include "Logger.h"
#include "Settings.h"

namespace fs = std::filesystem;

namespace
{
	struct Run
	{
		SimResult result;
		bool      bOk = false;
	};

	bool PrepareRun(const CSettings& base, const fs::path& pDir, CSettings& settings)
	{
		std::error_code ec;
		fs::remove_all(pDir, ec);
		fs::create_directories(pDir / "Log", ec);
		fs::create_directories(pDir / "Backup", ec);

		settings = base;
		settings.SetOutputFolders((pDir / "Log").string(), (pDir / "Backup").string());
		const fs::path pConfig = pDir / fs::path(base.strategy.strConfig).filename();
		fs::copy_file(base.strategy.strConfig, pConfig, fs::copy_options::overwrite_existing, ec);
		settings.strategy.strConfig = pConfig.string();
		return !ec;
	}

	Run Simulate(const CSettings& base, const fs::path& pDir, const EngineOptions& options, bool bInfo)
	{
		Run run;
		CSettings settings;
		if(!PrepareRun(base, pDir, settings))
			return run;

		Logger logger;
		logger.SetLogDir(settings.path.strLog);
		logger.SetEcho(false);
		logger.SetEnabled(LogLevel::Info, bInfo);
		logger.SetEnabled(LogLevel::Warning, bInfo);

		CEngine engine(logger);
		if(!engine.Setup(settings, options))
			return run;
		engine.run_here();

		run.result = engine.GetResult();
		run.bOk    = run.result.bFinished && !run.result.bError;
		return run;
	}

	std::string Describe(const SimResult& r)
	{
		char buf[512];
		std::snprintf(buf, sizeof(buf),
		              "equity %.2f (%+.2f%%), drawdown %.2f%%, cycles %lld (in profit %lld), liquidations %lld, "
		              "orders %lld, rejected %lld, fees %.2f, funding %.2f, cycle drawdown %.2f%%, max leverage %.2f, steps %lld",
		              r.dFinalEquity, r.dReturn, r.dMaxDrawdown, r.nCycles, r.nWins, r.nLiquidations, r.nOrders,
		              r.nRejected, r.dFees, r.dFunding, r.dWorstCycleDrawdown, r.dMaxLeverage, r.nSteps);
		return buf;
	}

	bool Same(const SimResult& a, const SimResult& b)
	{
		return a.dFinalEquity == b.dFinalEquity && a.dMaxDrawdown == b.dMaxDrawdown && a.nCycles == b.nCycles &&
		       a.nWins == b.nWins && a.nLiquidations == b.nLiquidations && a.nOrders == b.nOrders &&
		       a.nRejected == b.nRejected && a.dFees == b.dFees && a.dFunding == b.dFunding &&
		       a.dWorstCycleDrawdown == b.dWorstCycleDrawdown && a.dMaxLeverage == b.dMaxLeverage &&
		       a.nSteps == b.nSteps && a.nUnderwater == b.nUnderwater && a.bOpenAtEnd == b.bOpenAtEnd && a.strReason == b.strReason;
	}

	double Seconds(std::chrono::steady_clock::time_point tStart)
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
	}
}

int main(int argc, char* argv[])
{
	if(argc < 4)
	{
		std::cerr << "usage: SxeMultiRunTest <EngineConfig.ini> <work folder> <parallel runs>" << std::endl;
		return 1;
	}
	const std::string strConfig = fs::absolute(argv[1]).string();
	const fs::path    pWork     = fs::absolute(argv[2]);
	const int         nRuns     = std::max(1, std::atoi(argv[3]));

	CSettings base;
	if(!base.Load(strConfig, fs::path(strConfig).parent_path().string()))
	{
		for(const std::string& strError : base.GetErrors())
			std::cerr << strError << std::endl;
		return 1;
	}
	base.log.nConsole = 0;
	base.sim.nResume  = 0;

	auto tStart = std::chrono::steady_clock::now();
	const Run full = Simulate(base, pWork / "full", EngineOptions(), true);
	const double dFullSec = Seconds(tStart);
	if(!full.bOk)
	{
		std::cerr << "Full run failed, see " << (pWork / "full/Log/error.txt").string() << std::endl;
		return 1;
	}
	std::cout << "full:      " << Describe(full.result) << " [" << dFullSec << " s]" << std::endl;

	EngineOptions light;
	light.bArchive   = false;
	light.bStateFile = false;
	light.bOrderLog  = false;

	tStart = std::chrono::steady_clock::now();
	const Run single = Simulate(base, pWork / "single", light, false);
	const double dSingleSec = Seconds(tStart);
	bool bOk = single.bOk && Same(full.result, single.result);
	std::cout << "light:     " << (single.bOk ? Describe(single.result) : std::string("failed"))
	          << " [" << dSingleSec << " s]" << (bOk ? "" : "  <-- DIFFERENT") << std::endl;

	std::error_code ec;
	if(!fs::is_empty(pWork / "single/Backup", ec))
	{
		std::cout << "light:     files written in " << (pWork / "single/Backup").string() << std::endl;
		bOk = false;
	}

	std::vector<Run> vRuns(static_cast<size_t>(nRuns));
	std::vector<std::thread> vThreads;
	tStart = std::chrono::steady_clock::now();
	for(int i = 0; i < nRuns; i++)
		vThreads.emplace_back([&, i]{ vRuns[static_cast<size_t>(i)] = Simulate(base, pWork / ("run" + std::to_string(i + 1)), light, false); });
	for(std::thread& t : vThreads)
		t.join();
	const double dParallelSec = Seconds(tStart);

	for(int i = 0; i < nRuns; i++)
	{
		const Run& run = vRuns[static_cast<size_t>(i)];
		const bool bSame = run.bOk && Same(full.result, run.result);
		bOk = bOk && bSame;
		if(!bSame)
			std::cout << "parallel " << (i + 1) << ": " << (run.bOk ? Describe(run.result) : std::string("failed"))
			          << "  <-- DIFFERENT" << std::endl;
	}
	std::cout << "parallel:  " << nRuns << " runs in " << dParallelSec << " s (in sequence about "
	          << nRuns * dSingleSec << " s, " << (dParallelSec > 0 ? nRuns * dSingleSec / dParallelSec : 0)
	          << "x)" << std::endl;

	std::cout << (bOk ? "OK: all runs give the same numbers" : "ERROR: runs with different results") << std::endl;
	return bOk ? 0 : 1;
}
