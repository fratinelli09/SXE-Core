#include "CSimExchange.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>

#include "TimeUtils.h"

namespace
{
	const long long FUNDING_PERIOD = 8 * 3600;

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

CSimExchange::CSimExchange(const ExchangeRules& rules_, double dInitialCapital):
	rules(rules_),
	account(),
	position(),
	stats(),
	lastLiquidation(),
	bLiquidation(true),
	dFundingRate(0),
	dFundingMultiplier(1),
	nFundingSlot(-1),
	bLiquidated(false),
	nTime(0),
	dPrice(0)
{
	account.dWallet = dInitialCapital;
	_Update();
}

void CSimExchange::SetSimRules(bool bLiquidation_, double dFundingRate_, double dFundingMultiplier_)
{
	bLiquidation       = bLiquidation_;
	dFundingRate       = dFundingRate_;
	dFundingMultiplier = dFundingMultiplier_;
}

void CSimExchange::SetMaintenanceTiers(const std::vector<std::pair<double, double>>& vNewTiers)
{
	vTiers = vNewTiers;
	_Update();
}

double CSimExchange::MaintenanceMargin(double dNotional) const
{
	if(vTiers.empty())
		return dNotional * rules.dMaintenanceMargin;

	double dMargin = 0, dLow = 0;
	for(const auto& tier : vTiers)
	{
		if(dNotional <= tier.first)
			return dMargin + (dNotional - dLow) * tier.second;
		dMargin += (tier.first - dLow) * tier.second;
		dLow = tier.first;
	}
	return dMargin + (dNotional - dLow) * vTiers.back().second;
}

double CSimExchange::_LiquidationPrice() const
{
	const double dQty = position.dQty;
	if(dQty <= 0)
		return 0;

	const double dFixed = dQty * position.dAvgPrice - _Collateral();
	if(vTiers.empty())
	{
		const double dDen = dQty * (1.0 - rules.dMaintenanceMargin - rules.dFeeTaker);
		return (dDen > 0 && dFixed > 0) ? dFixed / dDen : 0;
	}

	double dCum = 0, dLow = 0;
	for(size_t i = 0; i <= vTiers.size(); i++)
	{
		const bool   bLast = (i == vTiers.size());
		const double dRate = bLast ? vTiers.back().second : vTiers[i].second;
		const double dDen  = dQty * (1.0 - dRate - rules.dFeeTaker);
		if(dDen <= 0)
			return 0;

		const double dPrice = (dFixed + dCum - dRate * dLow) / dDen;
		if(dPrice <= 0)
			return 0;
		if(bLast || dQty * dPrice <= vTiers[i].first)
			return dPrice;

		dCum += (vTiers[i].first - dLow) * dRate;
		dLow  = vTiers[i].first;
	}
	return 0;
}

bool CSimExchange::LoadFunding(const std::string& strFile, std::string& strError)
{
	vFunding.clear();
	if(strFile.empty())
		return true;

	std::ifstream csv(strFile);
	if(!csv)
	{
		strError = "Funding history cannot be read: " + strFile;
		return false;
	}

	std::string strLine;
	while(std::getline(csv, strLine))
	{
		const size_t nSep = strLine.find_first_of(",;");
		if(nSep == std::string::npos)
			continue;

		const long long nTimeRow = TimeUtils::Parse(strLine.substr(0, nSep));
		double dRate = 0;
		std::string strRate = strLine.substr(nSep + 1);
		if(!strRate.empty() && strRate.back() == '\r')
			strRate.pop_back();
		if(nTimeRow <= 0 || !ToDouble(strRate, dRate))
			continue;

		vFunding.emplace_back(nTimeRow / FUNDING_PERIOD, dRate);
	}
	std::sort(vFunding.begin(), vFunding.end());

	if(vFunding.empty())
	{
		strError = "No valid funding rate in " + strFile;
		return false;
	}
	return true;
}

bool CSimExchange::SetOrderLog(const std::string& strFile, bool bAppend)
{
	strOrderLog = strFile;

	std::ofstream orderLog(strOrderLog, bAppend ? std::ios::app : std::ios::trunc);
	return static_cast<bool>(orderLog);
}

double CSimExchange::_FundingRate(long long nSlot) const
{
	const auto it = std::lower_bound(vFunding.begin(), vFunding.end(), std::make_pair(nSlot, -1e300));
	if(it != vFunding.end() && it->first == nSlot)
		return it->second * dFundingMultiplier;
	return dFundingRate * dFundingMultiplier;
}

void CSimExchange::ApplyFunding(long long nCandleTime, double dMark)
{
	const long long nSlot = nCandleTime / FUNDING_PERIOD;

	if(nFundingSlot < 0 || !position.bOpen || position.dQty <= 0 || nSlot <= nFundingSlot)
	{
		if(nSlot > nFundingSlot)
			nFundingSlot = nSlot;
		return;
	}

	double dPaid = 0;
	for(long long s = nFundingSlot + 1; s <= nSlot; s++)
		dPaid += position.dQty * dMark * _FundingRate(s);

	account.dWallet       -= dPaid;
	position.dFundingPaid += dPaid;
	stats.dFundingPaid    += dPaid;
	if(rules.eMarginMode == MarginMode::Isolated)
		position.dMargin = std::max(0.0, position.dMargin - dPaid);
	nFundingSlot = nSlot;

	_Update();
}

bool CSimExchange::Refresh(long long nNewTime, double dNewPrice)
{
	nTime  = nNewTime;
	dPrice = dNewPrice;

	if(bLiquidation && position.bOpen && position.dQty > 0)
	{
		const double dEquity = _Collateral() + position.dQty * (dPrice - position.dAvgPrice);
		const double dMaint  = MaintenanceMargin(position.dQty * dPrice) + position.dQty * dPrice * rules.dFeeTaker;
		if(dEquity <= dMaint)
			_Liquidate();
	}

	_Update();
	return true;
}

bool CSimExchange::TakeLiquidated()
{
	const bool b = bLiquidated;
	bLiquidated = false;
	return b;
}

double CSimExchange::_Collateral() const
{
	return (rules.eMarginMode == MarginMode::Isolated) ? position.dMargin : account.dWallet;
}

void CSimExchange::_Liquidate()
{
	lastLiquidation.positionBefore = position;
	lastLiquidation.dPrice         = dPrice;

	account.dWallet = (rules.eMarginMode == MarginMode::Isolated) ? std::max(0.0, account.dWallet - position.dMargin) : 0.0;
	lastLiquidation.dWalletAfter = account.dWallet;

	position = Position();
	stats.nLiquidations++;
	bLiquidated = true;

	OrderResult r;
	r.bOk        = true;
	r.dFillPrice = dPrice;
	r.dQty       = lastLiquidation.positionBefore.dQty;
	_LogOrder("LIQUIDATION", r);
}

void CSimExchange::_Update()
{
	if(position.bOpen)
	{
		position.dUnrealizedPnL = (dPrice - position.dAvgPrice) * position.dQty;

		position.dLiquidationPrice = _LiquidationPrice();
	}

	account.dEquity = account.dWallet + (position.bOpen ? position.dUnrealizedPnL : 0);

	if(!position.bOpen)
		account.dAvailable = account.dWallet;
	else if(rules.eMarginMode == MarginMode::Isolated)
		account.dAvailable = account.dWallet - position.dMargin;
	else
		account.dAvailable = account.dEquity - position.dQty * dPrice / position.dLeverage;

	if(account.dAvailable < 0)
		account.dAvailable = 0;
}

OrderResult CSimExchange::MarketBuy(double dUsdt, double dLeverage)
{
	if(dPrice <= 0)
		return _Reject("price not available yet");
	if(!(dUsdt > 0))
		return _Reject("invalid amount");
	if(!(dLeverage >= 1))
		return _Reject("invalid leverage");

	double dQty = dUsdt * dLeverage / dPrice;
	if(rules.dQtyStep > 0)
		dQty = std::floor(dQty / rules.dQtyStep + 1e-9) * rules.dQtyStep;

	const double dFill = dPrice * (1.0 + rules.dSlippage);
	double dNotional = dQty * dFill;

	if(dQty <= 0 || dQty + 1e-12 < rules.dMinQty)
		return _Reject("quantity below the minimum");
	if(dNotional < rules.dMinNotional)
		return _Reject("order value below the minimum");

	double dFee    = dNotional * rules.dFeeTaker;
	double dNeeded = dNotional / dLeverage + dFee;

	if(rules.eMarginMode == MarginMode::Isolated && dNeeded > account.dAvailable + 1e-9 && account.dAvailable > 0)
	{
		const double dMarginMax = account.dAvailable / (1.0 + dLeverage * rules.dFeeTaker * (1.0 + rules.dSlippage));
		double dQtyMax = dMarginMax * dLeverage / dFill;
		if(rules.dQtyStep > 0)
			dQtyMax = std::floor(dQtyMax / rules.dQtyStep + 1e-9) * rules.dQtyStep;

		if(dQtyMax <= 0 || dQtyMax + 1e-12 < rules.dMinQty || dQtyMax * dFill < rules.dMinNotional)
			return _Reject("insufficient available margin");

		dQty      = dQtyMax;
		dNotional = dQtyMax * dFill;
		dFee      = dNotional * rules.dFeeTaker;
		dNeeded   = dNotional / dLeverage + dFee;
	}

	if(dNeeded > account.dAvailable + 1e-9)
		return _Reject("insufficient available margin");

	const double dOldQty = position.bOpen ? position.dQty : 0.0;
	const double dTotQty = dOldQty + dQty;
	position.dAvgPrice  = (position.dAvgPrice * dOldQty + dFill * dQty) / dTotQty;
	position.dQty       = dTotQty;
	position.dMargin   += dNotional / dLeverage;
	position.dLeverage  = dLeverage;
	position.dFeesPaid += dFee;
	position.bOpen      = true;

	account.dWallet -= dFee;
	stats.dFeesPaid += dFee;
	stats.nOrders++;

	_Update();

	OrderResult r;
	r.bOk        = true;
	r.dFillPrice = dFill;
	r.dQty       = dQty;
	r.dFee       = dFee;
	_LogOrder("BUY", r);
	return r;
}

OrderResult CSimExchange::Close()
{
	if(!position.bOpen)
		return _Reject("no open position");

	const double dFill = dPrice * (1.0 - rules.dSlippage);
	const double dQty  = position.dQty;
	const double dPnL  = (dFill - position.dAvgPrice) * dQty;
	const double dFee  = dQty * dFill * rules.dFeeTaker;

	account.dWallet += dPnL - dFee;
	stats.dFeesPaid    += dFee;
	stats.dRealizedPnL += dPnL;
	stats.nOrders++;
	stats.nClosed++;
	if(dPnL - dFee - position.dFeesPaid - position.dFundingPaid > 0)
		stats.nWins++;

	position = Position();
	_Update();

	OrderResult r;
	r.bOk        = true;
	r.dFillPrice = dFill;
	r.dQty       = dQty;
	r.dFee       = dFee;
	_LogOrder("CLOSE", r);
	return r;
}

CSimExchange::Snapshot CSimExchange::GetSnapshot() const
{
	Snapshot s;
	s.nTime        = nTime;
	s.dPrice       = dPrice;
	s.dWallet      = account.dWallet;
	s.position     = position;
	s.nFundingSlot = nFundingSlot;
	s.stats        = stats;
	return s;
}

void CSimExchange::Restore(const Snapshot& s)
{
	nTime           = s.nTime;
	dPrice          = s.dPrice;
	account.dWallet = s.dWallet;
	position        = s.position;
	nFundingSlot    = s.nFundingSlot;
	stats           = s.stats;
	_Update();
}

OrderResult CSimExchange::_Reject(const std::string& strError)
{
	stats.nRejected++;

	OrderResult r;
	r.bOk      = false;
	r.strError = strError;
	_LogOrder("REJECT", r);
	return r;
}

void CSimExchange::_LogOrder(const char* pszType, const OrderResult& r)
{
	if(strOrderLog.empty())
		return;

	std::error_code ec;
	const bool bNew = !std::filesystem::exists(strOrderLog, ec) || std::filesystem::file_size(strOrderLog, ec) == 0;

	std::ofstream orderLog(strOrderLog, std::ios::app);
	orderLog.imbue(std::locale::classic());
	orderLog << std::fixed;
	if(bNew)
		orderLog << "time;type;price;fill_price;qty;fee;wallet\n";

	orderLog << TimeUtils::Format(nTime) << ";" << pszType << ";" << std::setprecision(2) << dPrice << ";";
	if(r.bOk)
		orderLog << r.dFillPrice << ";" << std::setprecision(6) << r.dQty << ";" << std::setprecision(4) << r.dFee;
	else
		orderLog << r.strError << ";;";
	orderLog << ";" << std::setprecision(2) << account.dWallet << "\n";
}
