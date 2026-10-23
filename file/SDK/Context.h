#pragma once
#include <string>
#include <vector>

#include "Logger.h"
#include "Types.h"

struct StrategyInfo
{
	std::string strName;
	std::string strVersion;
	std::string strDescription;
};

class IOrders
{
public:
	virtual ~IOrders(){}

	virtual OrderResult MarketBuy(double dUsdt, double dLeverage) = 0;
	virtual OrderResult Close() = 0;

	virtual OrderResult LimitBuy(double dPrice, double dUsdt, double dLeverage) = 0;
	virtual OrderResult LimitClose(double dPrice) = 0;
	virtual OrderResult Move(long long nOrderId, double dPrice) = 0;
	virtual bool Cancel(long long nOrderId) = 0;
	virtual void CancelAll() = 0;
};

struct InitContext
{
	Mode          eMode;
	bool          bResume;
	std::string   strSymbol;
	long long     nStartTime;
	std::string   strLogDir;
	std::string   strBackupDir;

	Account       account;
	Position      position;
	ExchangeRules rules;

	Logger&       log;
};

struct StepContext
{
	long long     nTime;
	long long     nStep;
	long long     nCycle;
	double        dPrice;
	Candle        lastCandle;
	bool          bCandleClosed;

	Account       account;
	Position      position;

	bool          bLiquidated;

	std::vector<LimitOrder> vOpenOrders;
	std::vector<Fill>       vFills;

	IOrders&      orders;
	Logger&       log;
};
