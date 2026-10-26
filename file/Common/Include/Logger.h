#pragma once
#include <atomic>
#include <string>
#include <mutex>

#define LOG_FILE_NAME							"log.txt"
#define LOG_ERROR_FILE_NAME						"error.txt"

enum class LogLevel
{
	Info    = 0,
	Warning = 1,
	Error   = 2
};

class Logger
{
public:
	static Logger& Get();

	Logger();

	void SetLogDir(const std::string& strDir);

	void SetEnabled(LogLevel level, bool bEnabled);
	bool IsEnabled(LogLevel level) const;
	std::string EnabledLevels() const;

	void SetEngineTime(long long nTime) { nEngineTime = nTime; }

	void SetContext(const std::string& strContext);

	void SetEcho(bool bEcho);

	void Info(const std::string& strMsg);
	void Warning(const std::string& strMsg);
	void Error(const std::string& strMsg);

	void WarningOnce(const std::string& strMsg);
	void ErrorOnce(const std::string& strMsg);

	void ClearOnce();

private:
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	void _Write(LogLevel level, const std::string& strMsg);
	void _WriteOnce(LogLevel level, const std::string& strMsg);

	static std::string _Now();

private:
	mutable std::mutex mtx;
	std::string strLogFile;
	std::string strErrorFile;
	bool bEnabled[3];
	bool bEcho;
	std::atomic<long long> nEngineTime;
	std::string strContext;
	std::string strLastOnce;
	bool bOnceSinceClear;
};
