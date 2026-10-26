#include "sxe.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "CEngine.h"
#include "INIReader.h"
#include "Logger.h"
#include "Settings.h"

namespace fs = std::filesystem;

namespace
{
	enum class EngineState
	{
		Config,
		Ready,
		Failed,
		Done
	};

	struct ConfigSection
	{
		std::string strName;
		std::vector<std::pair<std::string, std::string>> vKeys;
	};

	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
		return s;
	}

	std::string LibraryDir()
	{
#ifdef _WIN32
		HMODULE hModule = nullptr;
		wchar_t szPath[MAX_PATH * 4] = {};
		if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		                       reinterpret_cast<LPCWSTR>(&sxe_version), &hModule) ||
		   GetModuleFileNameW(hModule, szPath, static_cast<DWORD>(sizeof(szPath) / sizeof(szPath[0]))) == 0)
			return fs::current_path().string();
		const fs::path pLib(szPath);
#else
		Dl_info info;
		if(dladdr(reinterpret_cast<void*>(&sxe_version), &info) == 0 || info.dli_fname == nullptr)
			return fs::current_path().string();
		const fs::path pLib(info.dli_fname);
#endif
		std::error_code ec;
		const fs::path pCanonical = fs::canonical(pLib, ec);
		return ec ? fs::current_path().string() : pCanonical.parent_path().string();
	}

	std::string Join(const std::vector<std::string>& vLines)
	{
		std::string strOut;
		for(const std::string& strLine : vLines)
			strOut += (strOut.empty() ? "" : "\n") + strLine;
		return strOut;
	}
}

struct sxe_engine
{
	Logger                   logger;
	std::unique_ptr<CEngine> pEngine;
	EngineState              eState = EngineState::Config;

	std::vector<ConfigSection> vConfig;
	std::string                strBaseDir;
	std::string                strConfigName = "(configuration set through the C interface)";

	EngineOptions options;
	bool          bLogFiles      = true;
	bool          bConsole       = false;
	long long     nProgressEvery = 1000;

	sxe_log_fn      fnLog      = nullptr;
	void*           pLogUser   = nullptr;
	sxe_progress_fn fnProgress = nullptr;
	void*           pProgressUser = nullptr;
	sxe_cycle_fn    fnCycle    = nullptr;
	void*           pCycleUser = nullptr;

	std::vector<std::string> vErrors;
	std::string              strError;
	std::string              strReason;
	sxe_result               result = sxe_result();

	sxe_engine():
		pEngine(std::make_unique<CEngine>(logger)),
		strBaseDir(LibraryDir())
	{
		result.reason = "";
	}

	void SetValue(const std::string& strSection, const std::string& strKey, const std::string& strValue)
	{
		const std::string strSec = Lower(strSection);
		const std::string strK   = Lower(strKey);
		auto itSec = std::find_if(vConfig.begin(), vConfig.end(), [&](const ConfigSection& s){ return s.strName == strSec; });
		if(itSec == vConfig.end())
		{
			vConfig.push_back(ConfigSection{strSec, {}});
			itSec = vConfig.end() - 1;
		}
		auto itKey = std::find_if(itSec->vKeys.begin(), itSec->vKeys.end(), [&](const std::pair<std::string, std::string>& k){ return k.first == strK; });
		if(itKey == itSec->vKeys.end())
			itSec->vKeys.emplace_back(strK, strValue);
		else
			itKey->second = strValue;
	}

	std::string ConfigText() const
	{
		std::string strText;
		for(const ConfigSection& section : vConfig)
		{
			strText += "[" + section.strName + "]\n";
			for(const std::pair<std::string, std::string>& key : section.vKeys)
			{
				std::string strValue = key.second;
				for(size_t nPos = strValue.find('\n'); nPos != std::string::npos; nPos = strValue.find('\n', nPos + 2))
					strValue.insert(nPos + 1, " ");
				strText += key.first + " = " + strValue + "\n";
			}
		}
		return strText;
	}

	sxe_status Fail(sxe_status status, const std::string& strMsg)
	{
		strError = strMsg;
		return status;
	}
};

namespace
{
	bool ValidName(const char* psz)
	{
		if(psz == nullptr || *psz == '\0')
			return false;
		const std::string s(psz);
		return s.find_first_of("[]=;#\r\n") == std::string::npos && s.find_first_not_of(" \t") != std::string::npos;
	}

	template<typename Fn>
	sxe_status Guard(sxe_engine* engine, Fn fn)
	{
		if(engine == nullptr)
			return SXE_ERR_ARGUMENT;
		try
		{
			engine->strError.clear();
			return fn();
		}
		catch(const std::bad_alloc&)
		{
			engine->strError = "out of memory";
		}
		catch(const std::exception& e)
		{
			engine->strError = std::string("unexpected error: ") + e.what();
		}
		catch(...)
		{
			engine->strError = "unexpected error";
		}
		return SXE_ERR_INTERNAL;
	}

	sxe_status Setup(sxe_engine* engine)
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "the engine has already been set up");

		CSettings settings;
		if(!settings.LoadText(engine->ConfigText(), engine->strConfigName, engine->strBaseDir))
		{
			engine->eState = EngineState::Failed;
			return engine->Fail(SXE_ERR_CONFIG, Join(settings.GetErrors()));
		}
		settings.log.nConsole = engine->bConsole ? 1 : 0;
		if(!settings.CreateOutputFolders())
		{
			engine->eState = EngineState::Failed;
			return engine->Fail(SXE_ERR_CONFIG, Join(settings.GetErrors()));
		}

		Logger& logger = engine->logger;
		logger.SetLogDir(settings.path.strLog);
		logger.SetFiles(engine->bLogFiles);
		logger.SetEcho(engine->bConsole);
		logger.SetEnabled(LogLevel::Info,    settings.log.nInfo    != 0);
		logger.SetEnabled(LogLevel::Warning, settings.log.nWarning != 0);
		logger.SetEnabled(LogLevel::Error,   settings.log.nError   != 0);
		logger.SetSink([engine](LogLevel level, const std::string& strMsg)
		{
			if(level == LogLevel::Error)
				engine->vErrors.push_back(strMsg);
			if(engine->fnLog)
				engine->fnLog(engine->pLogUser, static_cast<sxe_log_level>(level), strMsg.c_str());
		});

		EngineOptions options = engine->options;
		options.nProgressEvery = engine->nProgressEvery;
		if(engine->fnProgress)
		{
			options.fnProgress = [engine](const ProgressInfo& info)
			{
				sxe_progress progress;
				progress.time         = info.nTime;
				progress.step         = info.nStep;
				progress.cycle        = info.nCycle;
				progress.price        = info.dPrice;
				progress.equity       = info.dEquity;
				progress.drawdown_pct = info.dDrawdown;
				engine->fnProgress(engine->pProgressUser, &progress);
			};
		}
		if(engine->fnCycle)
		{
			options.fnCycle = [engine](const CCycleArchive::CycleRow& row)
			{
				sxe_cycle cycle;
				cycle.cycle         = row.nCycle;
				cycle.open_time     = row.nOpenTime;
				cycle.close_time    = row.nCloseTime;
				cycle.capital_start = row.dCapitalStart;
				cycle.capital_end   = row.dCapitalEnd;
				cycle.growth_pct    = row.dGrowth;
				cycle.drawdown_pct  = row.dDrawdown;
				cycle.max_leverage  = row.dMaxLeverage;
				cycle.buys          = row.nBuys;
				cycle.entry_price   = row.dEntryPrice;
				cycle.avg_price     = row.dAvgPrice;
				cycle.exit_price    = row.dExitPrice;
				cycle.fees          = row.dFees;
				cycle.funding       = row.dFunding;
				cycle.closed_by     = row.strClosedBy.c_str();
				engine->fnCycle(engine->pCycleUser, &cycle);
			};
		}

		engine->vErrors.clear();
		if(!engine->pEngine->Setup(settings, options))
		{
			engine->eState = EngineState::Failed;
			return engine->Fail(SXE_ERR_SETUP, engine->vErrors.empty() ? std::string("setup failed") : Join(engine->vErrors));
		}
		engine->vErrors.clear();
		engine->eState = EngineState::Ready;
		return SXE_OK;
	}
}

extern "C" {

SXE_API int sxe_api_version(void)
{
	return SXE_C_API_VERSION;
}

SXE_API const char* sxe_version(void)
{
	return SXE_VERSION_STRING;
}

SXE_API sxe_engine* sxe_engine_create(void)
{
	try
	{
		return new sxe_engine();
	}
	catch(...)
	{
		return nullptr;
	}
}

SXE_API void sxe_engine_destroy(sxe_engine* engine)
{
	try
	{
		delete engine;
	}
	catch(...)
	{
	}
}

SXE_API sxe_status sxe_engine_set_base_dir(sxe_engine* engine, const char* dir)
{
	return Guard(engine, [&]
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "configuration can only be changed before the setup");
		if(dir == nullptr || *dir == '\0')
			return engine->Fail(SXE_ERR_ARGUMENT, "base folder missing");
		std::error_code ec;
		if(!fs::is_directory(dir, ec))
			return engine->Fail(SXE_ERR_ARGUMENT, std::string("base folder not found: ") + dir);
		engine->strBaseDir = fs::absolute(dir, ec).lexically_normal().string();
		return SXE_OK;
	});
}

SXE_API sxe_status sxe_engine_load_config(sxe_engine* engine, const char* ini_path)
{
	return Guard(engine, [&]
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "configuration can only be changed before the setup");
		if(ini_path == nullptr || *ini_path == '\0')
			return engine->Fail(SXE_ERR_ARGUMENT, "configuration file missing");

		INIReader ini{std::string(ini_path)};
		if(ini.ParseError() < 0)
			return engine->Fail(SXE_ERR_CONFIG, std::string("Configuration file not found or not readable: ") + ini_path);
		if(ini.ParseError() > 0)
			return engine->Fail(SXE_ERR_CONFIG, "Invalid line " + std::to_string(ini.ParseError()) + " in " + ini_path);

		for(const std::string& strSection : ini.Sections())
			for(const std::string& strKey : ini.Keys(strSection))
				engine->SetValue(strSection, strKey, ini.Get(strSection, strKey, ""));
		engine->strConfigName = ini_path;
		return SXE_OK;
	});
}

SXE_API sxe_status sxe_engine_set(sxe_engine* engine, const char* section, const char* key, const char* value)
{
	return Guard(engine, [&]
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "configuration can only be changed before the setup");
		if(!ValidName(section) || !ValidName(key))
			return engine->Fail(SXE_ERR_ARGUMENT, "section or key missing or not valid");
		if(value == nullptr || std::string(value).find_first_of("\r\n") != std::string::npos)
			return engine->Fail(SXE_ERR_ARGUMENT, std::string("value of [") + section + "] " + key + " missing or on more lines");
		engine->SetValue(section, key, value);
		return SXE_OK;
	});
}

SXE_API sxe_status sxe_engine_set_option(sxe_engine* engine, sxe_option option, int64_t value)
{
	return Guard(engine, [&]
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "options can only be changed before the setup");

		const bool bFlag = (value == 0 || value == 1);
		switch(option)
		{
		case SXE_OPT_ARCHIVE:
		case SXE_OPT_STATE_FILE:
		case SXE_OPT_ORDER_LOG:
		case SXE_OPT_LOG_FILES:
		case SXE_OPT_CONSOLE:
			if(!bFlag)
				return engine->Fail(SXE_ERR_ARGUMENT, "option value must be 0 or 1");
			break;
		case SXE_OPT_PROGRESS_EVERY:
			if(value < 0)
				return engine->Fail(SXE_ERR_ARGUMENT, "progress interval must be >= 0");
			break;
		default:
			return engine->Fail(SXE_ERR_ARGUMENT, "unknown option " + std::to_string(static_cast<int>(option)));
		}

		switch(option)
		{
		case SXE_OPT_ARCHIVE:        engine->options.bArchive   = value != 0; break;
		case SXE_OPT_STATE_FILE:     engine->options.bStateFile = value != 0; break;
		case SXE_OPT_ORDER_LOG:      engine->options.bOrderLog  = value != 0; break;
		case SXE_OPT_LOG_FILES:      engine->bLogFiles          = value != 0; break;
		case SXE_OPT_CONSOLE:        engine->bConsole           = value != 0; break;
		case SXE_OPT_PROGRESS_EVERY: engine->nProgressEvery     = value;      break;
		default:                                                              break;
		}
		return SXE_OK;
	});
}

SXE_API sxe_status sxe_engine_on_log(sxe_engine* engine, sxe_log_fn fn, void* user)
{
	return Guard(engine, [&]
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "callbacks can only be changed before the setup");
		engine->fnLog    = fn;
		engine->pLogUser = user;
		return SXE_OK;
	});
}

SXE_API sxe_status sxe_engine_on_progress(sxe_engine* engine, sxe_progress_fn fn, void* user)
{
	return Guard(engine, [&]
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "callbacks can only be changed before the setup");
		engine->fnProgress    = fn;
		engine->pProgressUser = user;
		return SXE_OK;
	});
}

SXE_API sxe_status sxe_engine_on_cycle(sxe_engine* engine, sxe_cycle_fn fn, void* user)
{
	return Guard(engine, [&]
	{
		if(engine->eState != EngineState::Config)
			return engine->Fail(SXE_ERR_STATE, "callbacks can only be changed before the setup");
		engine->fnCycle    = fn;
		engine->pCycleUser = user;
		return SXE_OK;
	});
}

SXE_API sxe_status sxe_engine_setup(sxe_engine* engine)
{
	return Guard(engine, [&]{ return Setup(engine); });
}

SXE_API sxe_status sxe_engine_run(sxe_engine* engine)
{
	return Guard(engine, [&]
	{
		if(engine->eState == EngineState::Config)
		{
			const sxe_status status = Setup(engine);
			if(status != SXE_OK)
				return status;
		}
		if(engine->eState == EngineState::Done)
			return engine->Fail(SXE_ERR_STATE, "the engine has already run: create a new one for another run");
		if(engine->eState != EngineState::Ready)
			return engine->Fail(SXE_ERR_STATE, "the setup failed: create a new engine");

		engine->pEngine->run_here();
		engine->eState = EngineState::Done;

		const SimResult& r = engine->pEngine->GetResult();
		engine->strReason = r.bFinished ? r.strReason : std::string("the engine stopped because of an unexpected error");

		sxe_result& out = engine->result;
		out.finished                 = 1;
		out.error                    = (r.bError || !r.bFinished) ? 1 : 0;
		out.reason                   = engine->strReason.c_str();
		out.first_time               = r.nFirstTime;
		out.last_time                = r.nLastTime;
		out.initial_capital          = r.dInitialCapital;
		out.final_equity             = r.dFinalEquity;
		out.return_pct               = r.dReturn;
		out.max_drawdown_pct         = r.dMaxDrawdown;
		out.cycles                   = r.nCycles;
		out.wins                     = r.nWins;
		out.liquidations             = r.nLiquidations;
		out.orders                   = r.nOrders;
		out.rejected                 = r.nRejected;
		out.fees                     = r.dFees;
		out.funding                  = r.dFunding;
		out.worst_cycle_drawdown_pct = r.dWorstCycleDrawdown;
		out.max_leverage             = r.dMaxLeverage;
		out.open_at_end              = r.bOpenAtEnd ? 1 : 0;
		out.underwater               = r.nUnderwater;
		out.steps                    = r.nSteps;
		out.seconds                  = r.dSeconds;

		if(out.error)
		{
			std::vector<std::string> vLines = engine->vErrors;
			if(std::find(vLines.begin(), vLines.end(), engine->strReason) == vLines.end())
				vLines.insert(vLines.begin(), engine->strReason);
			return engine->Fail(SXE_ERR_RUN, Join(vLines));
		}
		return SXE_OK;
	});
}

SXE_API void sxe_engine_stop(sxe_engine* engine)
{
	if(engine != nullptr)
		engine->pEngine->RequestStop();
}

SXE_API const sxe_result* sxe_engine_result(const sxe_engine* engine)
{
	return engine != nullptr ? &engine->result : nullptr;
}

SXE_API const char* sxe_engine_error(const sxe_engine* engine)
{
	return engine != nullptr ? engine->strError.c_str() : "invalid engine";
}

}
