#pragma once
#include "Types.h"

class IExchange
{
public:
	virtual ~IExchange(){}

	virtual bool Refresh(long long nTime, double dPrice) = 0;

	virtual const Account&  GetAccount() const = 0;
	virtual const Position& GetPosition() const = 0;
	virtual const ExchangeRules& GetRules() const = 0;

	virtual bool TakeLiquidated() = 0;

	virtual OrderResult MarketBuy(double dUsdt, double dLeverage) = 0;
	virtual OrderResult Close() = 0;
};
