#include "Settings.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
    bool ParseInt(const std::string& s, int& out)
    {
        try
        {
            size_t pos = 0;
            int v = std::stoi(s, &pos);
            if (pos != s.size()) return false;
            out = v;
            return true;
        }
        catch (const std::exception&) { return false; }
    }

    bool ParseDouble(const std::string& s, double& out)
    {
        std::istringstream is(s);
        is.imbue(std::locale::classic());

        double v = 0;
        is >> v;

        if (is.fail() || is.peek() != std::char_traits<char>::eof())
            return false;

        out = v;
        return true;
    }

    std::string Key(const char* pszSection, const char* pszKey)
    {
        return std::string("[") + pszSection + "] " + pszKey;
    }

    std::string Num(double d)
    {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os << d;
        return os.str();
    }
}

CSettings::CSettings():
    bSimulate(true),
    bValid(false)
{
}

CSettings::~CSettings()
{
}

std::string CSettings::ResolvePath(const std::string& strPath, const std::string& strBaseDir)
{
    if (strPath.empty())
        return strPath;

    fs::path p(strPath);

    if (strPath == "~" || strPath.compare(0, 2, "~/") == 0)
    {
        const char* pszHome = std::getenv("HOME");
#ifdef _WIN32
        if (pszHome == nullptr)
            pszHome = std::getenv("USERPROFILE");
#endif
        p = fs::path(pszHome ? pszHome : "") / strPath.substr(strPath.size() > 1 ? 2 : 1);
    }
    else if (p.is_relative())
    {
        p = fs::path(strBaseDir) / p;
    }

    std::string strOut = p.lexically_normal().string();
    while (strOut.size() > 1 && strOut.back() == '/')
        strOut.pop_back();
    return strOut;
}

std::string CSettings::StrategyFileName(const std::string& strLibrary)
{
    if (strLibrary.empty() || strLibrary.find('.') != std::string::npos)
        return strLibrary;
#ifdef _WIN32
    return "lib" + strLibrary + ".dll";
#else
    return "lib" + strLibrary + ".so";
#endif
}

bool CSettings::Load(const std::string& strConfigFile, const std::string& strBaseDir_)
{
    bValid = false;
    vErrors.clear();
    strFilePath = strConfigFile;
    strBaseDir  = strBaseDir_;

    CReader ini(strConfigFile, false);

    if (ini.ParseError() == -1)
    {
        _AddError("Configuration file not found or not readable: " + strConfigFile);
        return false;
    }
    return _Load(ini);
}

bool CSettings::LoadText(const std::string& strText, const std::string& strName, const std::string& strBaseDir_)
{
    bValid = false;
    vErrors.clear();
    strFilePath = strName;
    strBaseDir  = strBaseDir_;

    CReader ini(strText.data(), strText.size());
    return _Load(ini);
}

bool CSettings::_Load(CReader& ini)
{
    if (ini.ParseError() > 0)
    {
        _AddError("Invalid line " + std::to_string(ini.ParseError()) + " in " + strFilePath);
        return false;
    }

    std::string strMode;
    if (_Required(ini, CFG_ENGINE_SECTION, CFG_ENGINE_MODE, strMode))
    {
        if (strMode == CFG_MODE_SIM)
            bSimulate = true;
        else if (strMode == CFG_MODE_REAL)
            bSimulate = false;
        else
            _AddError(Key(CFG_ENGINE_SECTION, CFG_ENGINE_MODE) + " = '" + strMode +
                      "' is not valid (allowed values: " CFG_MODE_SIM ", " CFG_MODE_REAL ")");
    }
    _Required(ini, CFG_ENGINE_SECTION, CFG_ENGINE_SYMBOL, strSymbol);

    const bool bFolder  = _Required(ini, CFG_STRATEGY_SECTION, CFG_STRATEGY_FOLDER, strategy.strFolder);
    const bool bLibrary = _Required(ini, CFG_STRATEGY_SECTION, CFG_STRATEGY_LIBRARY, strategy.strLibrary);
    const bool bConfig  = _Required(ini, CFG_STRATEGY_SECTION, CFG_STRATEGY_CONFIG, strategy.strConfig);
    strategy.strFolder = _Resolve(strategy.strFolder);
    strategy.strConfig = _Resolve(strategy.strConfig);

    strategy.strLibrary = StrategyFileName(strategy.strLibrary);

    if (bLibrary && strategy.strLibrary.find_first_of("/\\") != std::string::npos)
    {
        _AddError(Key(CFG_STRATEGY_SECTION, CFG_STRATEGY_LIBRARY) + " = '" + strategy.strLibrary +
                  "': give only the file name, the folder is " CFG_STRATEGY_FOLDER);
    }
    else if (bFolder && bLibrary)
    {
        std::error_code ec;
        if (!fs::is_directory(strategy.strFolder, ec))
            _AddError("Strategy folder not found: " + strategy.strFolder +
                      " (" + Key(CFG_STRATEGY_SECTION, CFG_STRATEGY_FOLDER) + ")");
        else if (!fs::is_regular_file(strategy.LibraryPath(), ec))
            _AddError("Strategy " + strategy.strLibrary + " not found in " + strategy.strFolder);
    }
    if (bConfig)
        _CheckFile(CFG_STRATEGY_SECTION, CFG_STRATEGY_CONFIG, strategy.strConfig);

    _Required(ini, CFG_PATH_SECTION, CFG_PATH_LOG, path.strLog);
    _Required(ini, CFG_PATH_SECTION, CFG_PATH_BACKUP, path.strBackup);
    path.strLog    = _Resolve(path.strLog);
    path.strBackup = _Resolve(path.strBackup);
    if (bSimulate && _Required(ini, CFG_PATH_SECTION, CFG_PATH_DATA, path.strData))
        _CheckFolder(CFG_PATH_SECTION, CFG_PATH_DATA, path.strData = _Resolve(path.strData));

    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MARGIN_MODE, exchange.strMarginMode);
    if (exchange.strMarginMode != CFG_MARGIN_CROSSED && exchange.strMarginMode != CFG_MARGIN_ISOLATED)
        _AddError(Key(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MARGIN_MODE) + " = '" + exchange.strMarginMode +
                  "' is not valid (allowed values: " CFG_MARGIN_CROSSED ", " CFG_MARGIN_ISOLATED ")");

    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_FEE_TAKER, exchange.dFeeTaker);
    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_FEE_MAKER, exchange.dFeeMaker);
    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_SLIPPAGE, exchange.dSlippage);
    std::string strTiers = "200000:0.004, 1000000:0.005, 5000000:0.007, 15000000:0.01, 50000000:0.02, "
                           "100000000:0.03, 150000000:0.06, 300000000:0.12, 500000000:0.15, "
                           "700000000:0.2, 900000000:0.3, 1200000000:0.6";
    if (ini.Has(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MAINT_MARGIN) && !ini.Has(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MAINT_TIERS))
    {
        _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MAINT_MARGIN, exchange.dMaintenanceMargin);
        strTiers = "1e15:" + Num(exchange.dMaintenanceMargin);
    }
    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MAINT_TIERS, strTiers);
    _ParseTiers(strTiers);
    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_QTY_STEP, exchange.dQtyStep);
    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MIN_QTY, exchange.dMinQty);
    _Optional(ini, CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MIN_NOTIONAL, exchange.dMinNotional);

    _CheckRange(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_FEE_TAKER, exchange.dFeeTaker, 0, 0.1);
    _CheckRange(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_FEE_MAKER, exchange.dFeeMaker, 0, 0.1);
    _CheckRange(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_SLIPPAGE, exchange.dSlippage, 0, 0.1);
    _CheckRange(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_QTY_STEP, exchange.dQtyStep, 0, 1e9);
    _CheckRange(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MIN_QTY, exchange.dMinQty, 0, 1e9);
    _CheckRange(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MIN_NOTIONAL, exchange.dMinNotional, 0, 1e9);

    sim.strStateFile = "../Log/last_row.txt";
    sim.strOrderLog  = "../Log/Set_Close_log.txt";

    if (bSimulate)
    {
        if (_Required(ini, CFG_SIM_SECTION, CFG_SIM_DATA_FILE, sim.strDataFile))
            _CheckFile(CFG_SIM_SECTION, CFG_SIM_DATA_FILE, sim.strDataFile = ResolvePath(sim.strDataFile, path.strData));

        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_FUNDING_FILE, sim.strFundingFile);
        if (!sim.strFundingFile.empty())
            _CheckFile(CFG_SIM_SECTION, CFG_SIM_FUNDING_FILE, sim.strFundingFile = ResolvePath(sim.strFundingFile, path.strData));

        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_STATE_FILE, sim.strStateFile);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_ORDER_LOG, sim.strOrderLog);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_RESUME, sim.nResume);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_FUNDING_RATE, sim.dFundingRate);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_FUNDING_MULTIPLIER, sim.dFundingMultiplier);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_INITIAL_CAPITAL, sim.dInitialCapital);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_START_DATE, sim.strStartDate);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_END_DATE, sim.strEndDate);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_INTRABAR, sim.nIntrabar);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_TICK_STEP, sim.dTickStep);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_LIQUIDATION, sim.nLiquidation);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_LIMIT_FILL, sim.strLimitFill);
        _Optional(ini, CFG_SIM_SECTION, CFG_SIM_SAVE_EVERY, sim.nSaveEvery);

        if (sim.strStateFile.empty())
            _AddError(Key(CFG_SIM_SECTION, CFG_SIM_STATE_FILE) + " is empty: a file is needed for the simulation state");
        if (sim.strOrderLog.empty())
            _AddError(Key(CFG_SIM_SECTION, CFG_SIM_ORDER_LOG) + " is empty: a file is needed for the order log");

        sim.strStateFile = _Resolve(sim.strStateFile);
        sim.strOrderLog  = _Resolve(sim.strOrderLog);

        _CheckFlag(CFG_SIM_SECTION, CFG_SIM_RESUME, sim.nResume);
        _CheckRange(CFG_SIM_SECTION, CFG_SIM_INTRABAR, sim.nIntrabar, 0, 3);
        _CheckRange(CFG_SIM_SECTION, CFG_SIM_SAVE_EVERY, sim.nSaveEvery, 1, 1000000);
        _CheckFlag(CFG_SIM_SECTION, CFG_SIM_LIQUIDATION, sim.nLiquidation);
        if (sim.strLimitFill != CFG_LIMIT_FILL_THROUGH && sim.strLimitFill != CFG_LIMIT_FILL_TOUCH)
            _AddError(Key(CFG_SIM_SECTION, CFG_SIM_LIMIT_FILL) + " = '" + sim.strLimitFill +
                      "' is not valid (allowed values: " CFG_LIMIT_FILL_THROUGH ", " CFG_LIMIT_FILL_TOUCH ")");
        _CheckRange(CFG_SIM_SECTION, CFG_SIM_FUNDING_RATE, sim.dFundingRate, -0.1, 0.1);
        _CheckRange(CFG_SIM_SECTION, CFG_SIM_FUNDING_MULTIPLIER, sim.dFundingMultiplier, 0, 10);
        _CheckRange(CFG_SIM_SECTION, CFG_SIM_TICK_STEP, sim.dTickStep, 0, 1);
        if (sim.dInitialCapital <= 0)
            _AddError(Key(CFG_SIM_SECTION, CFG_SIM_INITIAL_CAPITAL) + " = " + Num(sim.dInitialCapital) + ": must be > 0");

        const bool bStart = _CheckDate(CFG_SIM_SECTION, CFG_SIM_START_DATE, sim.strStartDate, false);
        const bool bEnd   = _CheckDate(CFG_SIM_SECTION, CFG_SIM_END_DATE, sim.strEndDate, true);
        if (bStart && bEnd && !sim.strStartDate.empty() && !sim.strEndDate.empty() && sim.strStartDate > sim.strEndDate)
            _AddError(Key(CFG_SIM_SECTION, CFG_SIM_START_DATE) + " " + sim.strStartDate + " is after " +
                      Key(CFG_SIM_SECTION, CFG_SIM_END_DATE) + " " + sim.strEndDate);
    }

    else
    {
        if (_Required(ini, CFG_REAL_SECTION, CFG_REAL_SCRIPT_DIR, real.strScriptDir))
            _CheckFolder(CFG_REAL_SECTION, CFG_REAL_SCRIPT_DIR, real.strScriptDir = _Resolve(real.strScriptDir));
        if (_Required(ini, CFG_REAL_SECTION, CFG_REAL_ENV_FILE, real.strEnvFile))
            _CheckFile(CFG_REAL_SECTION, CFG_REAL_ENV_FILE, real.strEnvFile = _Resolve(real.strEnvFile));

        _Optional(ini, CFG_REAL_SECTION, CFG_REAL_STEP_INTERVAL_MS, real.nStepIntervalMs);
        _CheckRange(CFG_REAL_SECTION, CFG_REAL_STEP_INTERVAL_MS, real.nStepIntervalMs, 0, 3600000);
    }

    _Optional(ini, CFG_LOG_SECTION, CFG_LOG_INFO, log.nInfo);
    _Optional(ini, CFG_LOG_SECTION, CFG_LOG_WARNING, log.nWarning);
    _Optional(ini, CFG_LOG_SECTION, CFG_LOG_ERROR, log.nError);
    _Optional(ini, CFG_LOG_SECTION, CFG_LOG_CONSOLE, log.nConsole);
    _CheckFlag(CFG_LOG_SECTION, CFG_LOG_INFO, log.nInfo);
    _CheckFlag(CFG_LOG_SECTION, CFG_LOG_WARNING, log.nWarning);
    _CheckFlag(CFG_LOG_SECTION, CFG_LOG_ERROR, log.nError);
    _CheckFlag(CFG_LOG_SECTION, CFG_LOG_CONSOLE, log.nConsole);

    bValid = vErrors.empty();
    return bValid;
}

void CSettings::_ParseTiers(const std::string& strTiers)
{
    exchange.vMaintenanceTiers.clear();
    const std::string strKey = Key(CFG_EXCHANGE_SECTION, CFG_EXCHANGE_MAINT_TIERS);

    std::string strAll = strTiers;
    std::replace(strAll.begin(), strAll.end(), '\n', ' ');
    std::stringstream ss(strAll);
    std::string strTier;
    double dPrevLimit = 0, dPrevRate = 0;
    while (std::getline(ss, strTier, ','))
    {
        const size_t nFirst = strTier.find_first_not_of(" \t");
        if (nFirst == std::string::npos)
            continue;
        strTier = strTier.substr(nFirst, strTier.find_last_not_of(" \t") - nFirst + 1);

        const size_t nSep = strTier.find(':');
        double dLimit = 0, dRate = 0;
        if (nSep == std::string::npos || !ParseDouble(strTier.substr(0, nSep), dLimit) ||
            !ParseDouble(strTier.substr(nSep + 1), dRate))
        {
            _AddError(strKey + ": tier '" + strTier + "' is not valid (format: limit_USDT:rate, e.g. 200000:0.004)");
            return;
        }
        if (dLimit <= dPrevLimit || dRate < 0 || dRate >= 1 || dRate < dPrevRate)
        {
            _AddError(strKey + ": '" + strTier + "': limits must increase and rates must be between 0 and 1, non-decreasing");
            return;
        }
        exchange.vMaintenanceTiers.emplace_back(dLimit, dRate);
        dPrevLimit = dLimit;
        dPrevRate  = dRate;
    }

    if (exchange.vMaintenanceTiers.empty())
    {
        _AddError(strKey + ": no tier");
        return;
    }
    exchange.dMaintenanceMargin = exchange.vMaintenanceTiers.front().second;
}

bool CSettings::CreateOutputFolders()
{
    std::vector<std::string> vFolders = { path.strLog, path.strBackup };
    if (bSimulate)
    {
        vFolders.push_back(fs::path(sim.strStateFile).parent_path().string());
        vFolders.push_back(fs::path(sim.strOrderLog).parent_path().string());
    }

    bool bOk = true;
    for (const std::string& strFolder : vFolders)
    {
        if (strFolder.empty())
            continue;

        std::error_code ec;
        fs::create_directories(strFolder, ec);
        if (!fs::is_directory(strFolder, ec))
        {
            _AddError("Cannot create the folder " + strFolder);
            bOk = false;
        }
    }
    if (!bOk)
        bValid = false;
    return bOk;
}

void CSettings::SetOutputFolders(const std::string& strLogDir, const std::string& strBackupDir)
{
    path.strLog    = strLogDir;
    path.strBackup = strBackupDir;
    sim.strStateFile = (fs::path(strLogDir) / fs::path(sim.strStateFile).filename()).string();
    sim.strOrderLog  = (fs::path(strLogDir) / fs::path(sim.strOrderLog).filename()).string();
}

bool CSettings::_Required(CReader& ini, const char* pszSection, const char* pszKey, std::string& strValue)
{
    if (!ini.Has(pszSection, pszKey) || !ini.Get(pszSection, pszKey, strValue) || strValue.empty())
    {
        _AddError("Required key missing or empty: " + Key(pszSection, pszKey));
        return false;
    }
    return true;
}

void CSettings::_Optional(CReader& ini, const char* pszSection, const char* pszKey, std::string& strValue)
{
    if (ini.Has(pszSection, pszKey))
        ini.Get(pszSection, pszKey, strValue);
}

void CSettings::_Optional(CReader& ini, const char* pszSection, const char* pszKey, double& dValue)
{
    if (ini.Has(pszSection, pszKey) && !ini.Get(pszSection, pszKey, dValue))
        _AddError(Key(pszSection, pszKey) + ": value is not a number");
}

void CSettings::_Optional(CReader& ini, const char* pszSection, const char* pszKey, int& nValue)
{
    if (ini.Has(pszSection, pszKey) && !ini.Get(pszSection, pszKey, nValue))
        _AddError(Key(pszSection, pszKey) + ": value is not an integer");
}

void CSettings::_CheckRange(const char* pszSection, const char* pszKey, double dValue, double dMin, double dMax)
{
    if (dValue < dMin || dValue > dMax)
        _AddError(Key(pszSection, pszKey) + " = " + Num(dValue) + " out of range [" + Num(dMin) + ", " + Num(dMax) + "]");
}

void CSettings::_CheckFlag(const char* pszSection, const char* pszKey, int nValue)
{
    if (nValue != 0 && nValue != 1)
        _AddError(Key(pszSection, pszKey) + " = " + std::to_string(nValue) + ": must be 0 or 1");
}

bool CSettings::_CheckDate(const char* pszSection, const char* pszKey, std::string& strDate, bool bEnd)
{
    if (strDate.empty())
        return true;

    bool bOk = (strDate.size() == 10 || strDate.size() == 7) && strDate[4] == '-' &&
               (strDate.size() == 7 || strDate[7] == '-');
    for (size_t i = 0; bOk && i < strDate.size(); i++)
        if (i != 4 && i != 7 && (strDate[i] < '0' || strDate[i] > '9'))
            bOk = false;

    if (bOk)
    {
        const int nYear  = std::stoi(strDate.substr(0, 4));
        const int nMonth = std::stoi(strDate.substr(5, 2));
        bOk = nMonth >= 1 && nMonth <= 12;

        if (bOk)
        {
            static const int DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            const bool bLeap = (nYear % 4 == 0 && nYear % 100 != 0) || nYear % 400 == 0;
            const int nDays  = DAYS[nMonth - 1] + (nMonth == 2 && bLeap ? 1 : 0);

            if (strDate.size() == 7)
            {
                const int nDay = bEnd ? nDays : 1;
                strDate += (nDay < 10 ? "-0" : "-") + std::to_string(nDay);
            }
            else
            {
                const int nDay = std::stoi(strDate.substr(8, 2));
                bOk = nDay >= 1 && nDay <= nDays;
            }
        }
    }

    if (!bOk)
        _AddError(Key(pszSection, pszKey) + " = '" + strDate + "': invalid date (formats: YYYY-MM-DD or YYYY-MM)");
    return bOk;
}

void CSettings::_CheckFile(const char* pszSection, const char* pszKey, const std::string& strPath)
{
    std::error_code ec;
    if (!fs::is_regular_file(strPath, ec))
        _AddError("File not found: " + strPath + " (" + Key(pszSection, pszKey) + ")");
}

void CSettings::_CheckFolder(const char* pszSection, const char* pszKey, const std::string& strPath)
{
    std::error_code ec;
    if (!fs::is_directory(strPath, ec))
        _AddError("Folder not found: " + strPath + " (" + Key(pszSection, pszKey) + ")");
}

bool CReader::IsValid() const
{
    return (bMemory || !strpath.empty()) && ini.ParseError() == 0;
}

bool CReader::Has(const std::string& strSection, const std::string& strAttrib) const
{
    return ini.HasValue(strSection, strAttrib);
}

bool CReader::CanWrite() const
{
    return !bMemory && !strpath.empty() && (ini.ParseError() == 0 || ini.ParseError() == -1);
}

bool CReader::EnsureKey(const std::string& strSection, const std::string& strAttrib, const std::string& strDefault)
{
    if (ini.HasValue(strSection, strAttrib))
        return true;

    if (!bCreate || !CanWrite())
        return false;

    return ini.Set(strSection, strAttrib, strDefault);
}

bool CReader::Get(std::string strSection, std::string strAttrib, int &nValue)
{
    if (strpath.empty() && !bMemory) return false;

    if (!ini.HasValue(strSection, strAttrib))
        return EnsureKey(strSection, strAttrib, std::to_string(nValue));

    int nParsed = 0;
    if (!ParseInt(ini.Get(strSection, strAttrib, ""), nParsed)) return false;

    nValue = nParsed;
    return true;
}

bool CReader::Get(std::string strSection, std::string strAttrib, double &dValue)
{
    if (strpath.empty() && !bMemory) return false;

    if (!ini.HasValue(strSection, strAttrib))
    {
        std::ostringstream os;
        os.imbue(std::locale::classic());
        os << std::setprecision(15) << dValue;
        return EnsureKey(strSection, strAttrib, os.str());
    }

    double dParsed = 0;
    if (!ParseDouble(ini.Get(strSection, strAttrib, ""), dParsed)) return false;

    dValue = dParsed;
    return true;
}

bool CReader::Get(std::string strSection, std::string strAttrib, std::string &strValue)
{
    if (strpath.empty() && !bMemory) return false;

    if (!ini.HasValue(strSection, strAttrib))
        return EnsureKey(strSection, strAttrib, strValue);

    strValue = ini.Get(strSection, strAttrib, "");
    return true;
}

bool CReader::Set(std::string strSection, std::string strAttrib, int nValue)
{
    return Set(strSection, strAttrib, std::to_string(nValue));
}

bool CReader::Set(std::string strSection, std::string strAttrib, double dValue)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::setprecision(15) << dValue;
    return Set(strSection, strAttrib, os.str());
}

bool CReader::Set(std::string strSection, std::string strAttrib, const char *pszValue)
{
    return Set(strSection, strAttrib, std::string(pszValue ? pszValue : ""));
}

bool CReader::Set(std::string strSection, std::string strAttrib, const std::string &strValue)
{
    if (!CanWrite()) return false;

    return ini.Set(strSection, strAttrib, strValue);
}
