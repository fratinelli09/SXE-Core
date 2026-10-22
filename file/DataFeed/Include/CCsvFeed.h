#pragma once
#include <string>
#include <vector>

#include "IDataFeed.h"

class CCsvFeed : public IDataFeed
{
public:
	CCsvFeed();

	bool Load(const std::string& strFile, const std::string& strStart, const std::string& strEnd);
	void SetTicks(int nIntrabarMode, double dTickStep);
	std::string GetLastError() const { return strLastError; }

	size_t Count() const { return vCandles.size(); }
	long long FirstTime() const { return vCandles.empty() ? 0 : vCandles.front().nTime; }
	long long LastTime() const  { return vCandles.empty() ? 0 : vCandles.back().nTime; }
	size_t SkippedRows() const  { return nSkippedRows; }
	size_t Gaps() const         { return nGaps; }
	long long Interval() const  { return nInterval; }
	size_t CandlesDone() const  { return !bStarted ? 0 : (nTick < vTicks.size() ? nCandle : nCandle + 1); }

	bool Next(MarketStep& step) override;
	double Progress() const override;

	long long CurrentCandleTime() const;
	size_t NextTick() const { return nTick; }
	bool Seek(long long nCandleTime, size_t nNextTick);

private:
	void _BuildTicks(size_t nIndex);

private:
	std::vector<Candle> vCandles;
	std::vector<double> vTicks;
	long long   nInterval;
	size_t      nCandle;
	size_t      nTick;
	bool        bStarted;
	int         nIntrabar;
	double      dTickStep;
	size_t      nSkippedRows;
	size_t      nGaps;
	std::string strLastError;
};
