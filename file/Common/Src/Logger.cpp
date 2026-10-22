#include "Logger.h"
#include "TimeUtils.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

Logger& Logger::Get()
{
    static Logger instance;
    return instance;
}

Logger::Logger():
    strLogFile(LOG_FILE_NAME),
    strErrorFile(LOG_ERROR_FILE_NAME),
    bEnabled{true, true, true},
    nEngineTime(0),
    strContext(),
    strLastOnce(),
    bOnceSinceClear(false)
{
}

void Logger::SetLogDir(const std::string& strDir)
{
    std::lock_guard<std::mutex> lock(mtx);
    const std::string strBase = strDir.empty() ? std::string() : strDir + "/";
    strLogFile   = strBase + LOG_FILE_NAME;
    strErrorFile = strBase + LOG_ERROR_FILE_NAME;
}

void Logger::SetEnabled(LogLevel level, bool bEnable)
{
    std::lock_guard<std::mutex> lock(mtx);
    bEnabled[static_cast<int>(level)] = bEnable;
}

bool Logger::IsEnabled(LogLevel level) const
{
    std::lock_guard<std::mutex> lock(mtx);
    return bEnabled[static_cast<int>(level)];
}

std::string Logger::EnabledLevels() const
{
    std::lock_guard<std::mutex> lock(mtx);

    static const char* NAMES[] = { "info", "warning", "error" };

    std::string strLevels;
    for(int i = 0; i < 3; i++)
    {
        if(!bEnabled[i]) continue;
        if(!strLevels.empty()) strLevels += ", ";
        strLevels += NAMES[i];
    }
    return strLevels.empty() ? "none" : strLevels;
}

void Logger::SetContext(const std::string& strNewContext)
{
    std::lock_guard<std::mutex> lock(mtx);
    strContext = strNewContext;
}

void Logger::Info(const std::string& strMsg)
{
    std::lock_guard<std::mutex> lock(mtx);
    _Write(LogLevel::Info, strMsg);
}

void Logger::Warning(const std::string& strMsg)
{
    std::lock_guard<std::mutex> lock(mtx);
    _Write(LogLevel::Warning, strMsg);
}

void Logger::Error(const std::string& strMsg)
{
    std::lock_guard<std::mutex> lock(mtx);
    _Write(LogLevel::Error, strMsg);
}

void Logger::WarningOnce(const std::string& strMsg)
{
    std::lock_guard<std::mutex> lock(mtx);
    _WriteOnce(LogLevel::Warning, strMsg);
}

void Logger::ErrorOnce(const std::string& strMsg)
{
    std::lock_guard<std::mutex> lock(mtx);
    _WriteOnce(LogLevel::Error, strMsg);
}

void Logger::ClearOnce()
{
    std::lock_guard<std::mutex> lock(mtx);

    if(bOnceSinceClear)
    {
        bOnceSinceClear = false;
        return;
    }

    if(strLastOnce.empty())
        return;

    const std::string strResolved = strLastOnce;
    strLastOnce.clear();
    _Write(LogLevel::Info, "Back to normal after: " + strResolved);
}

void Logger::_WriteOnce(LogLevel level, const std::string& strMsg)
{
    bOnceSinceClear = true;

    if(strMsg == strLastOnce)
        return;

    strLastOnce = strMsg;
    _Write(level, strMsg);
}

void Logger::_Write(LogLevel level, const std::string& strMsg)
{
    if(!bEnabled[static_cast<int>(level)])
        return;

    static const char* TAG[] = { "I", "W", "E" };

    std::string strLine = "| " + _Now() + " | [" + TAG[static_cast<int>(level)] + "] | ";
    const long long nTime = nEngineTime;
    if(nTime > 0)
        strLine += TimeUtils::Format(nTime) + " | ";
    if(!strContext.empty())
        strLine += strContext + " | ";
    strLine += strMsg;

    {
        std::ofstream file(strLogFile, std::ios::app);
        file << strLine << "\n";
    }

    if(level == LogLevel::Error)
    {
        std::ofstream file(strErrorFile, std::ios::app);
        file << strLine << "\n";
    }

    if(level == LogLevel::Error)
        std::cerr << "\n[ERROR] " << strMsg << std::endl;
    else if(level == LogLevel::Warning)
        std::cerr << "\n[WARNING] " << strMsg << std::endl;
}

std::string Logger::_Now()
{
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}
