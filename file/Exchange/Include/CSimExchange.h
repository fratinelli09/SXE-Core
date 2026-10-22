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
	};

public:
	CSimExchange(const ExchangeRules& rules, double dInitialCapital);

	void SetSimRules(bool bLiquidation, double dFundingRate, double dFundingMultiplier);
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

	const Stats& GetStats() const { return stats; }
	const Liquidation& GetLastLiquidation() const { return lastLiquidation; }

	Snapshot GetSnapshot() const;
	void Restore(const Snapshot& snapshot);

private:
	void _Update();
	double _Collateral() const;
	double _LiquidationPrice() const;
	double _FundingRate(long long nSlot) const;
	void _Liquidate();
	OrderResult _Reject(const std::string& strError);
	void _LogOrder(const char* pszType, const OrderResult& r);

private:
	ExchangeRules rules;
	Account       account;
	Position      position;
	Stats         stats;
	Liquidation   lastLiquidation;

	bool          bLiquidation;
	double        dFundingRate;
	double        dFundingMultiplier;
	std::vector<std::pair<double, double>> vTiers;
	std::vector<std::pair<long long, double>> vFunding;
	long long     nFundingSlot;
	bool          bLiquidated;

	long long     nTime;
	double        dPrice;

	std::string   strOrderLog;
};
