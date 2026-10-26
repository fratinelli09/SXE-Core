#ifndef CSETTINGS_H
#define CSETTINGS_H

#include <string>
#include <utility>
#include <vector>
#include <memory>

#include "INIReader.h"

#define ENGINE_CONFIG_FILE_NAME             "EngineConfig.ini"

#define CFG_ENGINE_SECTION                  "ENGINE"
#define CFG_ENGINE_MODE                     "mode"
#define CFG_ENGINE_SYMBOL                   "symbol"
#define CFG_MODE_SIM                        "SIM"
#define CFG_MODE_REAL                       "REAL"

#define CFG_STRATEGY_SECTION                "STRATEGY"
#define CFG_STRATEGY_FOLDER                 "folder"
#define CFG_STRATEGY_LIBRARY                "library"
#define CFG_STRATEGY_CONFIG                 "config"

#define CFG_PATH_SECTION                    "PATH"
#define CFG_PATH_LOG                        "log"
#define CFG_PATH_BACKUP                     "backup"
#define CFG_PATH_DATA                       "data"

#define CFG_EXCHANGE_SECTION                "EXCHANGE"
#define CFG_EXCHANGE_MARGIN_MODE            "margin_mode"
#define CFG_EXCHANGE_FEE_TAKER              "fee_taker"
#define CFG_EXCHANGE_FEE_MAKER              "fee_maker"
#define CFG_EXCHANGE_SLIPPAGE               "slippage"
#define CFG_EXCHANGE_MAINT_MARGIN           "maintenance_margin"
#define CFG_EXCHANGE_MAINT_TIERS            "maintenance_tiers"
#define CFG_EXCHANGE_QTY_STEP               "qty_step"
#define CFG_EXCHANGE_MIN_QTY                "min_qty"
#define CFG_EXCHANGE_MIN_NOTIONAL           "min_notional"
#define CFG_MARGIN_CROSSED                  "crossed"
#define CFG_MARGIN_ISOLATED                 "isolated"

#define CFG_SIM_SECTION                     "SIMULATION"
#define CFG_SIM_DATA_FILE                   "data_file"
#define CFG_SIM_FUNDING_FILE                "funding_file"
#define CFG_SIM_STATE_FILE                  "state_file"
#define CFG_SIM_ORDER_LOG                   "order_log"
#define CFG_SIM_RESUME                      "resume"
#define CFG_SIM_FUNDING_RATE                "funding_rate"
#define CFG_SIM_FUNDING_MULTIPLIER          "funding_multiplier"
#define CFG_SIM_INITIAL_CAPITAL             "initial_capital"
#define CFG_SIM_START_DATE                  "start_date"
#define CFG_SIM_END_DATE                    "end_date"
#define CFG_SIM_INTRABAR                    "intrabar"
#define CFG_SIM_TICK_STEP                   "tick_step"
#define CFG_SIM_LIQUIDATION                 "liquidation"
#define CFG_SIM_LIMIT_FILL                  "limit_fill"
#define CFG_LIMIT_FILL_THROUGH              "through"
#define CFG_LIMIT_FILL_TOUCH                "touch"
#define CFG_SIM_SAVE_EVERY                  "save_every"

#define CFG_REAL_SECTION                    "REAL"
#define CFG_REAL_SCRIPT_DIR                 "script_dir"
#define CFG_REAL_ENV_FILE                   "env_file"
#define CFG_REAL_STEP_INTERVAL_MS           "step_interval_ms"

#define CFG_LOG_SECTION                     "LOG"
#define CFG_LOG_INFO                        "info"
#define CFG_LOG_WARNING                     "warning"
#define CFG_LOG_ERROR                       "error"
#define CFG_LOG_CONSOLE                     "console"

class CReader
{
public:
    explicit CReader(const std::string& path, bool bCreateMissing = true):
        strpath(path),
        bCreate(bCreateMissing),
        ini(path)
    {}
    ~CReader(){}

    bool IsValid() const;
    int  ParseError() const { return ini.ParseError(); }
    bool Has(const std::string& strSection, const std::string& strAttrib) const;

    bool Get(std::string strSection, std::string strAttrib, int &nValue);
    bool Get(std::string strSection, std::string strAttrib, double &dValue);
    bool Get(std::string strSection, std::string strAttrib, std::string &strValue);

    bool Set(std::string strSection, std::string strAttrib, int nValue);
    bool Set(std::string strSection, std::string strAttrib, double dValue);
    bool Set(std::string strSection, std::string strAttrib, const std::string &strValue);
    bool Set(std::string strSection, std::string strAttrib, const char *pszValue);

    void SetCreateMissing(bool bCreateMissing) { bCreate = bCreateMissing; }

private:
    bool CanWrite() const;
    bool EnsureKey(const std::string& strSection, const std::string& strAttrib, const std::string& strDefault);

    std::string strpath;
    bool        bCreate;
    INIReader   ini;
};

class CSettings
{
public:
    struct StrategySettings
    {
        std::string strFolder;
        std::string strLibrary;
        std::string strConfig;
        std::string LibraryPath() const { return strFolder + "/" + strLibrary; }
    };

    struct PathSettings
    {
        std::string strLog;
        std::string strBackup;
        std::string strData;
    };

    struct ExchangeSettings
    {
        std::string strMarginMode      = CFG_MARGIN_ISOLATED;
        double      dFeeTaker          = 0.0006;
        double      dFeeMaker          = 0.0002;
        double      dSlippage          = 0.0002;
        double      dMaintenanceMargin = 0.004;
        std::vector<std::pair<double, double>> vMaintenanceTiers;
        double      dQtyStep           = 0.0001;
        double      dMinQty            = 0.0001;
        double      dMinNotional       = 5;
    };

    struct SimSettings
    {
        std::string strDataFile;
        std::string strFundingFile;
        std::string strStateFile;
        std::string strOrderLog;
        int         nResume          = 0;
        double      dFundingRate     = 0.0001;
        double      dFundingMultiplier = 1;
        double      dInitialCapital  = 5000;
        std::string strStartDate;
        std::string strEndDate;
        int         nIntrabar        = 1;
        double      dTickStep        = 0.002;
        int         nLiquidation     = 1;
        std::string strLimitFill     = CFG_LIMIT_FILL_THROUGH;
        int         nSaveEvery       = 500;
    };

    struct RealSettings
    {
        std::string strScriptDir;
        std::string strEnvFile;
        int         nStepIntervalMs  = 1000;
    };

    struct LogSettings
    {
        int nInfo    = 1;
        int nWarning = 1;
        int nError   = 1;
        int nConsole = 1;
    };

public:
    CSettings();
    ~CSettings();

    bool Load(const std::string& strConfigFile, const std::string& strBaseDir);

    bool CreateOutputFolders();

    void SetOutputFolders(const std::string& strLogDir, const std::string& strBackupDir);

    bool IsValid() const                            { return bValid; }
    const std::vector<std::string>& GetErrors() const { return vErrors; }
    std::string GetFilePath() const                 { return strFilePath; }

    static std::string ResolvePath(const std::string& strPath, const std::string& strBaseDir);

    std::unique_ptr<CReader> getReader(std::string path, bool bCreateMissing = true) const
    {
        return std::make_unique<CReader>(path, bCreateMissing);
    }

public:
    bool             bSimulate;
    std::string      strSymbol;
    StrategySettings strategy;
    PathSettings     path;
    ExchangeSettings exchange;
    SimSettings      sim;
    RealSettings     real;
    LogSettings      log;

private:
    void _AddError(const std::string& strError) { vErrors.push_back(strError); }

    bool _Required(CReader& ini, const char* pszSection, const char* pszKey, std::string& strValue);
    void _Optional(CReader& ini, const char* pszSection, const char* pszKey, std::string& strValue);
    void _Optional(CReader& ini, const char* pszSection, const char* pszKey, double& dValue);
    void _Optional(CReader& ini, const char* pszSection, const char* pszKey, int& nValue);

    void _CheckRange(const char* pszSection, const char* pszKey, double dValue, double dMin, double dMax);
    void _ParseTiers(const std::string& strTiers);
    void _CheckFlag(const char* pszSection, const char* pszKey, int nValue);
    bool _CheckDate(const char* pszSection, const char* pszKey, std::string& strDate, bool bEnd);
    void _CheckFile(const char* pszSection, const char* pszKey, const std::string& strPath);
    void _CheckFolder(const char* pszSection, const char* pszKey, const std::string& strPath);

    std::string _Resolve(const std::string& strPath) const { return ResolvePath(strPath, strBaseDir); }

private:
    std::string              strFilePath;
    std::string              strBaseDir;
    std::vector<std::string> vErrors;
    bool                     bValid;
};

#endif
