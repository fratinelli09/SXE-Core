#pragma once
#include <vector>

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

	virtual OrderResult LimitBuy(double dPrice, double dUsdt, double dLeverage) = 0;
	virtual OrderResult LimitClose(double dPrice) = 0;
	virtual OrderResult Move(long long nOrderId, double dPrice) = 0;
	virtual bool Cancel(long long nOrderId) = 0;
	virtual void CancelAll() = 0;

	virtual const std::vector<LimitOrder>& GetOpenOrders() const = 0;
	virtual std::vector<Fill> TakeFills() = 0;
};
