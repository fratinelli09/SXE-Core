#pragma once
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

public:
	CCycleArchive();

	bool Setup(const std::string& strLogDir, const std::string& strBackupDir,
	           const std::vector<std::string>& vKeep, std::string& strError);

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

private:
	std::string strLogDir;
	std::string strBackupDir;
	std::string strTotalFile;
	std::string strCycleDir;
	std::vector<std::string> vKeep;

	long long nCycle;
	bool      bOpen;
	long long nOpenTime;
	double    dWalletStart;
	double    dFirstPrice;
	int       nBuys;
	double    dMinEquityPerc;
	double    dMaxLeverage;
};
