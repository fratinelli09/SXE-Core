#include "CCsvFeed.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>

#include "TimeUtils.h"

namespace
{
	void SplitLine(const std::string& strLine, std::vector<std::string>& vFields)
	{
		vFields.clear();
		std::string strField;
		for(char c : strLine)
		{
			if(c == ',' || c == ';')
			{
				vFields.push_back(strField);
				strField.clear();
			}
			else if(c != '\r' && c != '"')
			{
				strField += c;
			}
		}
		vFields.push_back(strField);
	}

	bool ToDouble(const std::string& s, double& d)
	{
		const char* pBegin = s.data();
		const char* pEnd   = s.data() + s.size();
		while(pBegin < pEnd && *pBegin == ' ')
			++pBegin;
		const std::from_chars_result r = std::from_chars(pBegin, pEnd, d);
		return r.ec == std::errc() && r.ptr != pBegin;
	}
}

namespace
{
	const int MAX_TICK_INTERMEDI = 1000;
}

CCsvFeed::CCsvFeed():
	nInterval(0),
	nCandle(0),
	nTick(0),
	bStarted(false),
	nIntrabar(1),
	dTickStep(0),
	nSkippedRows(0),
	nGaps(0)
{
	vTicks.reserve(4 * MAX_TICK_INTERMEDI + 4);
}

void CCsvFeed::SetTicks(int nIntrabarMode, double dStep)
{
	nIntrabar = nIntrabarMode;
	dTickStep = dStep;
}

bool CCsvFeed::Load(const std::string& strFile, const std::string& strStart, const std::string& strEnd)
{
	vCandles.clear();
	vTicks.clear();
	nCandle = 0;
	nTick = 0;
	bStarted = false;
	nSkippedRows = 0;
	nGaps = 0;

	std::ifstream csv(strFile);
	if(!csv)
	{
		strLastError = "Candle file cannot be read: " + strFile;
		return false;
	}

	const long long nStart = strStart.empty() ? -1 : TimeUtils::Parse(strStart);
	const long long nEnd   = strEnd.empty()   ? -1 : TimeUtils::Parse(strEnd) + 24 * 3600;

	std::string strLine;
	std::vector<std::string> vFields;
	long long nRow = 0;
	long long nPrevTime = -1;

	while(std::getline(csv, strLine))
	{
		++nRow;
		if(strLine.empty() || strLine == "\r")
			continue;

		SplitLine(strLine, vFields);

		const bool bSplitTime = vFields.size() >= 6 && vFields[1].find(':') != std::string::npos &&
		                        vFields[1].find('-') == std::string::npos;
		const size_t nPrice = bSplitTime ? 2 : 1;

		Candle c;
		const bool bOk = vFields.size() >= nPrice + 4 &&
		                 (c.nTime = TimeUtils::Parse(bSplitTime ? vFields[0] + " " + vFields[1] : vFields[0])) > 0 &&
		                 ToDouble(vFields[nPrice], c.dOpen) && ToDouble(vFields[nPrice + 1], c.dHigh) &&
		                 ToDouble(vFields[nPrice + 2], c.dLow) && ToDouble(vFields[nPrice + 3], c.dClose) &&
		                 c.dOpen > 0 && c.dHigh > 0 && c.dLow > 0 && c.dClose > 0;
		if(!bOk)
		{
			if(nRow > 1)
				++nSkippedRows;
			continue;
		}
		if(vFields.size() >= nPrice + 5)
			ToDouble(vFields[nPrice + 4], c.dVolume);

		if(nStart >= 0 && c.nTime < nStart)
			continue;
		if(nEnd >= 0 && c.nTime >= nEnd)
			break;

		if(c.nTime <= nPrevTime)
		{
			strLastError = "Row " + std::to_string(nRow) + " of " + strFile + " is not in time order (" +
			               vFields[0] + ")";
			return false;
		}
		nPrevTime = c.nTime;

		vCandles.push_back(c);
	}

	if(vCandles.empty())
	{
		strLastError = "No valid candle in " + strFile + " in the requested period";
		return false;
	}

	nInterval = vCandles.size() > 1 ? vCandles[1].nTime - vCandles[0].nTime : 0;
	for(size_t i = 1; i < vCandles.size(); i++)
		if(vCandles[i].nTime - vCandles[i - 1].nTime > nInterval)
			++nGaps;
	return true;
}

void CCsvFeed::_BuildTicks(size_t nIndex)
{
	vTicks.clear();
	const Candle& c = vCandles[nIndex];

	double vPoints[4];
	int nPoints = 0;
	if(nIntrabar == 0)
	{
		vPoints[nPoints++] = c.dClose;
	}
	else
	{
		const bool bHighFirst = (nIntrabar == 3) || (nIntrabar == 1 && c.dClose < c.dOpen);
		vPoints[nPoints++] = c.dOpen;
		vPoints[nPoints++] = bHighFirst ? c.dHigh : c.dLow;
		vPoints[nPoints++] = bHighFirst ? c.dLow  : c.dHigh;
		vPoints[nPoints++] = c.dClose;
	}

	double dCur = nIndex > 0 ? vCandles[nIndex - 1].dClose : 0;

	for(int i = 0; i < nPoints; i++)
	{
		const double p = vPoints[i];

		if(dCur > 0 && dTickStep > 0)
		{
			int n = static_cast<int>(std::ceil(std::fabs(p - dCur) / (dCur * dTickStep)));
			n = std::min(n, MAX_TICK_INTERMEDI);
			for(int k = 1; k < n; k++)
				vTicks.push_back(dCur + (p - dCur) * k / n);
		}

		if(vTicks.empty() || vTicks.back() != p)
			vTicks.push_back(p);

		dCur = p;
	}
}

bool CCsvFeed::Next(MarketStep& step)
{
	if(!bStarted || nTick >= vTicks.size())
	{
		const size_t nNextCandle = bStarted ? nCandle + 1 : 0;
		if(nNextCandle >= vCandles.size())
			return false;

		nCandle  = nNextCandle;
		nTick    = 0;
		bStarted = true;
		_BuildTicks(nCandle);
	}

	const Candle& c = vCandles[nCandle];
	const size_t nCount = vTicks.size();
	const size_t i = nTick++;

	step.nTime       = c.nTime + nInterval * static_cast<long long>(i + 1) / static_cast<long long>(nCount);
	step.dPrice      = vTicks[i];
	step.nCandleTime = c.nTime;
	step.bFirstTick  = (i == 0);

	step.bCandleClosed = (i + 1 == nCount);
	if(step.bCandleClosed)
		step.lastCandle = c;
	else
		step.lastCandle = nCandle > 0 ? vCandles[nCandle - 1] : Candle();
	return true;
}

double CCsvFeed::Progress() const
{
	return vCandles.empty() ? 1.0 : static_cast<double>(CandlesDone()) / static_cast<double>(vCandles.size());
}

long long CCsvFeed::CurrentCandleTime() const
{
	return bStarted ? vCandles[nCandle].nTime : -1;
}

bool CCsvFeed::Seek(long long nCandleTime, size_t nNextTick)
{
	const auto it = std::lower_bound(vCandles.begin(), vCandles.end(), nCandleTime,
	                                 [](const Candle& c, long long t){ return c.nTime < t; });
	if(it == vCandles.end() || it->nTime != nCandleTime)
		return false;

	nCandle  = static_cast<size_t>(it - vCandles.begin());
	bStarted = true;
	_BuildTicks(nCandle);
	nTick = std::min(nNextTick, vTicks.size());
	return true;
}
