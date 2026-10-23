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
	dPrice(0),
	bLimitThrough(true),
	dLimitTick(0),
	nNextOrderId(1),
	nOrdersVersion(0)
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

void CSimExchange::SetLimitRules(bool bThrough, double dTickStep)
{
	bLimitThrough = bThrough;
	dLimitTick    = dTickStep;
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

	_CheckOrders();

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

	while(!vOrders.empty())
		_Remove(vOrders.size() - 1, "liquidation");
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
	account.dAvailable -= _Reserved();

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

	const OrderResult r = _ApplyBuy(dQty, dFill, dLeverage, rules.dFeeTaker, OrderType::Market);
	_LogOrder("BUY", r);
	return r;
}

OrderResult CSimExchange::_ApplyBuy(double dQty, double dFill, double dLeverage, double dFeeRate, OrderType eType)
{
	const double dNotional = dQty * dFill;
	const double dFee      = dNotional * dFeeRate;

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
	r.eType      = eType;
	r.dFeeRate   = dFeeRate;
	return r;
}

OrderResult CSimExchange::Close()
{
	if(!position.bOpen)
		return _Reject("no open position");

	const OrderResult r = _ApplyClose(dPrice * (1.0 - rules.dSlippage), rules.dFeeTaker, OrderType::Market);
	_LogOrder("CLOSE", r);
	_CancelCloseOrders("position closed");
	return r;
}

OrderResult CSimExchange::_ApplyClose(double dFill, double dFeeRate, OrderType eType)
{
	lastClosed = position;
	const double dQty  = position.dQty;
	const double dPnL  = (dFill - position.dAvgPrice) * dQty;
	const double dFee  = dQty * dFill * dFeeRate;

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
	r.eType      = eType;
	r.dFeeRate   = dFeeRate;
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
	s.vOrders      = vOrders;
	s.nNextOrderId = nNextOrderId;
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
	vOrders         = s.vOrders;
	nNextOrderId    = s.nNextOrderId;
	nOrdersVersion++;
	_Update();
}

OrderResult CSimExchange::_Reject(const std::string& strError, OrderType eType)
{
	stats.nRejected++;

	OrderResult r;
	r.bOk      = false;
	r.strError = strError;
	r.eType    = eType;
	_LogOrder("REJECT", r);
	return r;
}

void CSimExchange::_LogOrder(const char* pszType, const OrderResult& r, double dLimitPrice)
{
	if(strOrderLog.empty())
		return;

	std::error_code ec;
	const bool bNew = !std::filesystem::exists(strOrderLog, ec) || std::filesystem::file_size(strOrderLog, ec) == 0;

	std::ofstream orderLog(strOrderLog, std::ios::app);
	orderLog.imbue(std::locale::classic());
	orderLog << std::fixed;
	if(bNew)
		orderLog << "time;type;price;fill_price;qty;fee;wallet;order;id;limit_price;fee_rate\n";

	orderLog << TimeUtils::Format(nTime) << ";" << pszType << ";" << std::setprecision(2) << dPrice << ";";
	if(r.bOk)
		orderLog << r.dFillPrice << ";" << std::setprecision(6) << r.dQty << ";" << std::setprecision(4) << r.dFee;
	else
		orderLog << r.strError << ";;";
	orderLog << ";" << std::setprecision(2) << account.dWallet << ";" << (r.eType == OrderType::Limit ? "limit" : "market") << ";";
	if(r.nOrderId > 0)
		orderLog << r.nOrderId;
	orderLog << ";";
	if(dLimitPrice > 0)
		orderLog << dLimitPrice;
	orderLog << ";";
	if(r.bOk && r.dFeeRate > 0)
		orderLog << std::setprecision(6) << r.dFeeRate;
	orderLog << "\n";
}

double CSimExchange::_Reserved() const
{
	double dReserved = 0;
	for(const LimitOrder& order : vOrders)
		dReserved += order.dReserved;
	return dReserved;
}

double CSimExchange::_BuyQty(double dLimit, double dUsdt, double dLeverage) const
{
	double dQty = dUsdt * dLeverage / dLimit;
	if(rules.dQtyStep > 0)
		dQty = std::floor(dQty / rules.dQtyStep + 1e-9) * rules.dQtyStep;
	return dQty;
}

std::string CSimExchange::_CheckLimit(OrderSide eSide, double dLimit, double dQty) const
{
	if(!(dLimit > 0))
		return "invalid limit price";
	if(dPrice <= 0)
		return "price not available yet";
	if(eSide == OrderSide::Buy)
	{
		if(dLimit >= dPrice)
			return "buy limit at or above the market price: it would be filled at once as a taker order";
		if(dQty <= 0 || dQty + 1e-12 < rules.dMinQty)
			return "quantity below the minimum";
		if(dQty * dLimit < rules.dMinNotional)
			return "order value below the minimum";
	}
	else if(dLimit <= dPrice)
	{
		return "close limit at or below the market price: it would be filled at once as a taker order";
	}
	return std::string();
}

OrderResult CSimExchange::LimitBuy(double dLimit, double dUsdt, double dLeverage)
{
	if(!(dUsdt > 0))
		return _Reject("invalid amount", OrderType::Limit);
	if(!(dLeverage >= 1))
		return _Reject("invalid leverage", OrderType::Limit);

	const double dQty = (dLimit > 0) ? _BuyQty(dLimit, dUsdt, dLeverage) : 0;
	const std::string strError = _CheckLimit(OrderSide::Buy, dLimit, dQty);
	if(!strError.empty())
		return _Reject(strError, OrderType::Limit);

	const double dReserved = dQty * dLimit / dLeverage + dQty * dLimit * rules.dFeeMaker;
	if(dReserved > account.dAvailable + 1e-9)
		return _Reject("insufficient available margin", OrderType::Limit);

	LimitOrder order;
	order.nId       = nNextOrderId++;
	order.eSide     = OrderSide::Buy;
	order.dPrice    = dLimit;
	order.dUsdt     = dUsdt;
	order.dLeverage = dLeverage;
	order.dQty      = dQty;
	order.dReserved = dReserved;
	order.nTime     = nTime;
	vOrders.push_back(order);
	nOrdersVersion++;
	stats.nLimitOrders++;
	_Update();

	OrderResult r;
	r.bOk      = true;
	r.dQty     = dQty;
	r.eType    = OrderType::Limit;
	r.dFeeRate = rules.dFeeMaker;
	r.nOrderId = order.nId;
	_LogOrder("LIMIT_BUY", r, dLimit);
	return r;
}

OrderResult CSimExchange::LimitClose(double dLimit)
{
	if(!position.bOpen)
		return _Reject("no open position", OrderType::Limit);
	if(std::any_of(vOrders.begin(), vOrders.end(), [](const LimitOrder& o){ return o.eSide == OrderSide::Close; }))
		return _Reject("a close limit is already open: move it or cancel it", OrderType::Limit);

	const std::string strError = _CheckLimit(OrderSide::Close, dLimit, position.dQty);
	if(!strError.empty())
		return _Reject(strError, OrderType::Limit);

	LimitOrder order;
	order.nId    = nNextOrderId++;
	order.eSide  = OrderSide::Close;
	order.dPrice = dLimit;
	order.dQty   = position.dQty;
	order.nTime  = nTime;
	vOrders.push_back(order);
	nOrdersVersion++;
	stats.nLimitOrders++;

	OrderResult r;
	r.bOk      = true;
	r.dQty     = position.dQty;
	r.eType    = OrderType::Limit;
	r.dFeeRate = rules.dFeeMaker;
	r.nOrderId = order.nId;
	_LogOrder("LIMIT_CLOSE", r, dLimit);
	return r;
}

OrderResult CSimExchange::Move(long long nOrderId, double dLimit)
{
	const auto it = std::find_if(vOrders.begin(), vOrders.end(), [&](const LimitOrder& o){ return o.nId == nOrderId; });
	if(it == vOrders.end())
		return _Reject("order " + std::to_string(nOrderId) + " not open", OrderType::Limit);

	LimitOrder moved = *it;
	moved.dPrice = dLimit;
	if(moved.eSide == OrderSide::Buy)
	{
		moved.dQty      = (dLimit > 0) ? _BuyQty(dLimit, moved.dUsdt, moved.dLeverage) : 0;
		moved.dReserved = moved.dQty * dLimit / moved.dLeverage + moved.dQty * dLimit * rules.dFeeMaker;
	}
	const std::string strError = _CheckLimit(moved.eSide, dLimit, moved.dQty);
	if(!strError.empty())
		return _Reject(strError, OrderType::Limit);
	if(moved.dReserved - it->dReserved > account.dAvailable + 1e-9)
		return _Reject("insufficient available margin", OrderType::Limit);

	*it = moved;
	nOrdersVersion++;
	_Update();

	OrderResult r;
	r.bOk      = true;
	r.dQty     = moved.eSide == OrderSide::Buy ? moved.dQty : position.dQty;
	r.eType    = OrderType::Limit;
	r.dFeeRate = rules.dFeeMaker;
	r.nOrderId = nOrderId;
	_LogOrder("MOVE", r, dLimit);
	return r;
}

bool CSimExchange::Cancel(long long nOrderId)
{
	for(size_t i = 0; i < vOrders.size(); i++)
		if(vOrders[i].nId == nOrderId)
		{
			_Remove(i, "cancelled by the strategy");
			_Update();
			return true;
		}
	return false;
}

void CSimExchange::CancelAll()
{
	while(!vOrders.empty())
		_Remove(vOrders.size() - 1, "cancelled by the strategy");
	_Update();
}

void CSimExchange::_Remove(size_t nIndex, const std::string& strReason)
{
	const LimitOrder order = vOrders[nIndex];
	vOrders.erase(vOrders.begin() + static_cast<long>(nIndex));
	nOrdersVersion++;
	stats.nCancelled++;

	OrderResult r;
	r.eType    = OrderType::Limit;
	r.nOrderId = order.nId;
	r.strError = strReason;
	_LogOrder("CANCEL", r, order.dPrice);
}

void CSimExchange::_CancelCloseOrders(const std::string& strReason)
{
	for(size_t i = vOrders.size(); i-- > 0; )
		if(vOrders[i].eSide == OrderSide::Close)
			_Remove(i, strReason);
}

bool CSimExchange::_Triggered(const LimitOrder& order) const
{
	if(order.eSide == OrderSide::Buy)
		return bLimitThrough ? (dLimitTick > 0 ? dPrice <= order.dPrice * (1.0 - dLimitTick) : dPrice < order.dPrice)
		                     : dPrice <= order.dPrice;
	return bLimitThrough ? (dLimitTick > 0 ? dPrice >= order.dPrice * (1.0 + dLimitTick) : dPrice > order.dPrice)
	                     : dPrice >= order.dPrice;
}

void CSimExchange::_CheckOrders()
{
	for(size_t i = 0; i < vOrders.size(); )
	{
		const LimitOrder order = vOrders[i];
		if(!_Triggered(order) || (order.eSide == OrderSide::Close && !position.bOpen))
		{
			++i;
			continue;
		}
		vOrders.erase(vOrders.begin() + static_cast<long>(i));
		nOrdersVersion++;
		const double dWalletBefore = account.dWallet;

		OrderResult r;
		if(order.eSide == OrderSide::Buy)
		{
			_Update();
			const double dNeeded = order.dQty * order.dPrice / order.dLeverage + order.dQty * order.dPrice * rules.dFeeMaker;
			if(dNeeded > account.dAvailable + 1e-9)
			{
				stats.nCancelled++;
				OrderResult rejected;
				rejected.eType    = OrderType::Limit;
				rejected.nOrderId = order.nId;
				rejected.strError = "insufficient margin at fill: order cancelled";
				_LogOrder("CANCEL", rejected, order.dPrice);
				continue;
			}
			r = _ApplyBuy(order.dQty, order.dPrice, order.dLeverage, rules.dFeeMaker, OrderType::Limit);
			r.nOrderId = order.nId;
			_LogOrder("FILL_BUY", r, order.dPrice);
		}
		else
		{
			r = _ApplyClose(order.dPrice, rules.dFeeMaker, OrderType::Limit);
			r.nOrderId = order.nId;
			_LogOrder("FILL_CLOSE", r, order.dPrice);
			_CancelCloseOrders("position closed");
		}
		stats.nLimitFills++;
		stats.dMakerFees += r.dFee;

		Fill fill;
		fill.nOrderId = order.nId;
		fill.eSide    = order.eSide;
		fill.eType    = OrderType::Limit;
		fill.nTime    = nTime;
		fill.dPrice   = r.dFillPrice;
		fill.dQty     = r.dQty;
		fill.dFee     = r.dFee;
		fill.dFeeRate = r.dFeeRate;
		fill.dWalletBefore = dWalletBefore;
		vFills.push_back(fill);
		i = 0;
	}
	_Update();
}

std::vector<Fill> CSimExchange::TakeFills()
{
	std::vector<Fill> vTaken;
	vTaken.swap(vFills);
	return vTaken;
}
