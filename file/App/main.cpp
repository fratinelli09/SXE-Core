#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

#include "CEngine.h"
#include "Logger.h"
#include "Settings.h"

namespace fs = std::filesystem;

static std::atomic<bool> g_bStop(false);

static void OnSignal(int)
{
	g_bStop = true;
}

static std::string ExecutableDir(const char* pszArgv0)
{
	std::error_code ec;
	fs::path pExe = fs::read_symlink("/proc/self/exe", ec);
	if(ec)
		pExe = fs::absolute(pszArgv0, ec);
	return pExe.parent_path().string();
}

static void LogSummary(const CSettings& settings)
{
	Logger& logger = Logger::Get();

	logger.Info("Configuration: " + settings.GetFilePath());
	logger.Info(std::string("Mode: ") + (settings.bSimulate ? CFG_MODE_SIM : CFG_MODE_REAL) +
	            ", symbol " + settings.strSymbol);
	logger.Info("Strategy: " + settings.strategy.LibraryPath() + " (config " + settings.strategy.strConfig + ")");
	logger.Info("Folders: log " + settings.path.strLog + ", backup " + settings.path.strBackup);
	logger.Info("Exchange: margin " + settings.exchange.strMarginMode +
	            ", fee taker " + std::to_string(settings.exchange.dFeeTaker) +
	            ", slippage " + std::to_string(settings.exchange.dSlippage));

	if(settings.bSimulate)
	{
		const CSettings::SimSettings& sim = settings.sim;
		logger.Info("Simulation: data " + sim.strDataFile +
		            (sim.strFundingFile.empty() ? ", fixed funding " + std::to_string(sim.dFundingRate)
		                                        : ", funding " + sim.strFundingFile));
		logger.Info("Simulation: initial capital " + std::to_string(sim.dInitialCapital) +
		            (sim.nResume ? ", resuming from " : ", from scratch (state in ") + sim.strStateFile + (sim.nResume ? "" : ")"));
		logger.Info("Simulation: orders " + sim.strOrderLog);
	}
	else
	{
		logger.Info("Real: scripts " + settings.real.strScriptDir + ", keys " + settings.real.strEnvFile +
		            ", step every " + std::to_string(settings.real.nStepIntervalMs) + " ms");
	}
}

int main(int argc, char* argv[])
{
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);

	const std::string strBinDir = ExecutableDir(argv[0]);
	const std::string strConfig = (argc > 1) ? CSettings::ResolvePath(argv[1], fs::current_path().string())
	                                         : CSettings::ResolvePath("../Config/" ENGINE_CONFIG_FILE_NAME, strBinDir);

	Logger& logger = Logger::Get();

	CSettings settings;
	const bool bLoaded = settings.Load(strConfig, strBinDir);

	const std::string strLogDir = settings.path.strLog.empty() ? CSettings::ResolvePath("../Log", strBinDir)
	                                                           : settings.path.strLog;
	std::error_code ec;
	fs::create_directories(strLogDir, ec);
	logger.SetLogDir(strLogDir);

	if(bLoaded)
	{
		logger.SetEnabled(LogLevel::Info,    settings.log.nInfo    != 0);
		logger.SetEnabled(LogLevel::Warning, settings.log.nWarning != 0);
		logger.SetEnabled(LogLevel::Error,   settings.log.nError   != 0);
	}

	logger.Info("==================== SXE START ====================");

	if(!bLoaded || !settings.CreateOutputFolders())
	{
		for(const std::string& strError : settings.GetErrors())
			logger.Error(strError);
		logger.Error("Invalid configuration (" + strConfig + "): engine not started");
		return 1;
	}

	LogSummary(settings);

	CEngine engine;
	if(!engine.Setup(settings))
	{
		logger.Error("Setup failed: engine not started");
		return 1;
	}

	const auto tStart = std::chrono::steady_clock::now();
	if(g_bStop)
		engine.RequestStop();
	engine.start();

	while(!engine.is_done())
	{
		if(g_bStop)
			engine.RequestStop();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	engine.wait();

	const double dSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
	logger.Info("Duration: " + std::to_string(dSec) + " s");
	logger.Info("===================== SXE END ======================");
	return engine.ExitCode();
}
