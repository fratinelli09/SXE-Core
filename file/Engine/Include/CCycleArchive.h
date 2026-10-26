#pragma once
#include <functional>
#include <string>
#include <vector>

#include "Types.h"

class CCycleArchive
{
public:
	struct Snapshot
	{
		bool      bOpen          = false;
		long long nOpenTime      = 0;
		double    dWalletStart   = 0;
		double    dFirstPrice    = 0;
		int       nBuys          = 0;
		double    dMinEquityPerc = 0;
		double    dMaxLeverage   = 0;
	};

	struct Totals
	{
		long long nCycles        = 0;
		double    dWorstDrawdown = 0;
		double    dMaxLeverage   = 0;
		double    dHours         = 0;
	};

	struct CycleRow
	{
		long long   nCycle        = 0;
		long long   nOpenTime     = 0;
		long long   nCloseTime    = 0;
		double      dCapitalStart = 0;
		double      dCapitalEnd   = 0;
		double      dGrowth       = 0;
		double      dDrawdown     = 0;
		double      dMaxLeverage  = 0;
		int         nBuys         = 0;
		double      dEntryPrice   = 0;
		double      dAvgPrice     = 0;
		double      dExitPrice    = 0;
		double      dFees         = 0;
		double      dFunding      = 0;
		std::string strClosedBy;
	};

public:
	CCycleArchive();

	bool Setup(const std::string& strLogDir, const std::string& strBackupDir,
	           const std::vector<std::string>& vKeep, std::string& strError, bool bWrite = true);

	long long CurrentCycle() const { return nCycle; }
	bool IsOpen() const { return bOpen; }

	void WriteNote(const std::string& strNote);

	void OnBuy(long long nTime, const OrderResult& r, double dWalletBefore);
	void OnStep(const Account& account, const Position& position);

	std::string Close(long long nTime, double dExitPrice, const Position& positionBefore,
	                  double dCloseFee, double dWalletAfter, const std::string& strReason, std::string& strError);

	bool MoveLogFiles(std::string& strError);

	Snapshot GetSnapshot() const;
	void Restore(const Snapshot& snapshot);

	const Totals& GetTotals() const { return totals; }

	void SetCycleCallback(std::function<void(const CycleRow&)> fnCallback) { fnCycle = std::move(fnCallback); }

private:
	std::string strLogDir;
	std::string strBackupDir;
	std::string strTotalFile;
	std::string strCycleDir;
	std::vector<std::string> vKeep;
	bool      bWrite;
	Totals    totals;
	std::function<void(const CycleRow&)> fnCycle;

	long long nCycle;
	bool      bOpen;
	long long nOpenTime;
	double    dWalletStart;
	double    dFirstPrice;
	int       nBuys;
	double    dMinEquityPerc;
	double    dMaxLeverage;
};
