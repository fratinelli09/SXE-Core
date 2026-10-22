#pragma once
#include "Types.h"

struct MarketStep
{
	long long nTime         = 0;
	double    dPrice        = 0;
	long long nCandleTime   = 0;
	bool      bFirstTick    = false;
	Candle    lastCandle;
	bool      bCandleClosed = false;
};

class IDataFeed
{
public:
	virtual ~IDataFeed(){}

	virtual bool Next(MarketStep& step) = 0;

	virtual double Progress() const = 0;
};
