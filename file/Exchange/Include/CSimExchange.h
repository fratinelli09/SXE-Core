#pragma once
#include <string>
#include <utility>
#include <vector>

#include "IExchange.h"

class CSimExchange : public IExchange
{
public:
	struct Stats
	{
		long long nOrders       = 0;
		long long nRejected     = 0;
		long long nClosed       = 0;
		long long nWins         = 0;
		long long nLiquidations = 0;
		double    dFeesPaid     = 0;
		double    dFundingPaid  = 0;
		double    dRealizedPnL  = 0;
		long long nLimitOrders  = 0;
		long long nLimitFills   = 0;
		long long nCancelled    = 0;
		double    dMakerFees    = 0;
	};

	struct Liquidation
	{
		Position positionBefore;
		double   dPrice       = 0;
		double   dWalletAfter = 0;
	};

	struct Snapshot
	{
		long long nTime         = 0;
		double    dPrice        = 0;
		double    dWallet       = 0;
		Position  position;
		long long nFundingSlot  = -1;
		Stats     stats;
		std::vector<LimitOrder> vOrders;
		long long nNextOrderId  = 1;
	};

public:
	CSimExchange(const ExchangeRules& rules, double dInitialCapital);

	void SetSimRules(bool bLiquidation, double dFundingRate, double dFundingMultiplier);
	void SetLimitRules(bool bThrough, double dTickStep);
	void SetMaintenanceTiers(const std::vector<std::pair<double, double>>& vTiers);
	double MaintenanceMargin(double dNotional) const;
	bool LoadFunding(const std::string& strFile, std::string& strError);
	size_t FundingCount() const { return vFunding.size(); }

	bool SetOrderLog(const std::string& strFile, bool bAppend);

	void ApplyFunding(long long nCandleTime, double dMark);

	bool Refresh(long long nTime, double dPrice) override;

	const Account&  GetAccount() const override  { return account; }
	const Position& GetPosition() const override { return position; }
	const ExchangeRules& GetRules() const override { return rules; }
	bool TakeLiquidated() override;

	OrderResult MarketBuy(double dUsdt, double dLeverage) override;
	OrderResult Close() override;

	OrderResult LimitBuy(double dPrice, double dUsdt, double dLeverage) override;
	OrderResult LimitClose(double dPrice) override;
	OrderResult Move(long long nOrderId, double dPrice) override;
	bool Cancel(long long nOrderId) override;
	void CancelAll() override;

	const std::vector<LimitOrder>& GetOpenOrders() const override { return vOrders; }
	std::vector<Fill> TakeFills() override;

	const Stats& GetStats() const { return stats; }
	const Liquidation& GetLastLiquidation() const { return lastLiquidation; }
	const Position& GetLastClosed() const { return lastClosed; }
	long long OrdersVersion() const { return nOrdersVersion; }

	Snapshot GetSnapshot() const;
	void Restore(const Snapshot& snapshot);

private:
	void _Update();
	double _Collateral() const;
	double _LiquidationPrice() const;
	double _FundingRate(long long nSlot) const;
	void _Liquidate();
	OrderResult _Reject(const std::string& strError, OrderType eType = OrderType::Market);
	void _LogOrder(const char* pszType, const OrderResult& r, double dLimitPrice = 0);
	OrderResult _ApplyBuy(double dQty, double dFill, double dLeverage, double dFeeRate, OrderType eType);
	OrderResult _ApplyClose(double dFill, double dFeeRate, OrderType eType);
	void _CheckOrders();
	bool _Triggered(const LimitOrder& order) const;
	double _Reserved() const;
	double _BuyQty(double dPrice, double dUsdt, double dLeverage) const;
	std::string _CheckLimit(OrderSide eSide, double dLimit, double dQty) const;
	void _Remove(size_t nIndex, const std::string& strReason);
	void _CancelCloseOrders(const std::string& strReason);

private:
	ExchangeRules rules;
	Account       account;
	Position      position;
	Stats         stats;
	Liquidation   lastLiquidation;
	Position      lastClosed;

	bool          bLiquidation;
	double        dFundingRate;
	double        dFundingMultiplier;
	std::vector<std::pair<double, double>> vTiers;
	std::vector<std::pair<long long, double>> vFunding;
	long long     nFundingSlot;
	bool          bLiquidated;

	long long     nTime;
	double        dPrice;

	bool          bLimitThrough;
	double        dLimitTick;
	std::vector<LimitOrder> vOrders;
	std::vector<Fill> vFills;
	long long     nNextOrderId;
	long long     nOrdersVersion;

	std::string   strOrderLog;
};
