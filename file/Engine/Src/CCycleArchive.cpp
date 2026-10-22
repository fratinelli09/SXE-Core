#include "CCycleArchive.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

#include "TimeUtils.h"

namespace fs = std::filesystem;

#define TOTAL_RESULT_FILE		"total_result.txt"
#define CYCLE_RESULT_FILE		"result.txt"
#define CYCLE_DIR_PREFIX		"b"
#define TOTAL_RESULT_HEADER		"CYCLE;OPEN;CLOSE;CAPITAL_START;CAPITAL_END;GROWTH%;DRAWDOWN%;MAX_LEVERAGE;BUYS;ENTRY_PRICE;AVG_PRICE;EXIT_PRICE;FEES;FUNDING;CLOSED_BY"

namespace
{
	std::string Num(double d, int nDec = 2)
	{
		std::ostringstream os;
		os.imbue(std::locale::classic());
		os.setf(std::ios::fixed);
		os.precision(nDec);
		os << d;
		return os.str();
	}
}

CCycleArchive::CCycleArchive():
	nCycle(1),
	bOpen(false),
	nOpenTime(0),
	dWalletStart(0),
	dFirstPrice(0),
	nBuys(0),
	dMinEquityPerc(0),
	dMaxLeverage(0)
{
}

bool CCycleArchive::Setup(const std::string& strLog, const std::string& strBackup,
                          const std::vector<std::string>& vKeepFiles, std::string& strError)
{
	strLogDir    = strLog;
	strBackupDir = strBackup;
	strTotalFile = strBackupDir + "/" TOTAL_RESULT_FILE;
	vKeep        = vKeepFiles;

	long long nMax = 0;
	std::error_code ec;
	for(const fs::directory_entry& e : fs::directory_iterator(strBackupDir, ec))
	{
		const std::string strName = e.path().filename().string();
		if(!e.is_directory(ec) || strName.size() < 2 || strName.compare(0, 1, CYCLE_DIR_PREFIX) != 0)
			continue;
		if(!std::all_of(strName.begin() + 1, strName.end(), [](char c){ return c >= '0' && c <= '9'; }))
			continue;
		nMax = std::max(nMax, std::stoll(strName.substr(1)));
	}
	if(ec)
	{
		strError = "Backup folder cannot be read: " + strBackupDir + " (" + ec.message() + ")";
		return false;
	}
	nCycle = nMax + 1;

	if(!fs::exists(strTotalFile, ec))
	{
		std::ofstream total(strTotalFile);
		total << TOTAL_RESULT_HEADER << "\n";
		if(!total)
		{
			strError = "Cannot write " + strTotalFile;
			return false;
		}
	}
	return true;
}

void CCycleArchive::WriteNote(const std::string& strNote)
{
	std::ofstream total(strTotalFile, std::ios::app);
	total << "# " << strNote << "\n";
}

void CCycleArchive::OnBuy(long long nTime, const OrderResult& r, double dWalletBefore)
{
	if(!bOpen)
	{
		bOpen          = true;
		nOpenTime      = nTime;
		dWalletStart   = dWalletBefore;
		dFirstPrice    = r.dFillPrice;
		nBuys          = 0;
		dMinEquityPerc = 0;
		dMaxLeverage   = 0;
	}
	++nBuys;
}

void CCycleArchive::OnStep(const Account& account, const Position& position)
{
	if(!bOpen || dWalletStart <= 0)
		return;

	const double dEquityPerc = (account.dEquity - dWalletStart) / dWalletStart * 100.0;
	if(dEquityPerc < dMinEquityPerc)
		dMinEquityPerc = dEquityPerc;

	const double dLeverage = position.dQty * position.dAvgPrice / dWalletStart;
	if(dLeverage > dMaxLeverage)
		dMaxLeverage = dLeverage;
}

std::string CCycleArchive::Close(long long nTime, double dExitPrice, const Position& positionBefore,
                                 double dCloseFee, double dWalletAfter, const std::string& strReason, std::string& strError)
{
	const double dGrowth = (dWalletStart > 0) ? (dWalletAfter - dWalletStart) / dWalletStart * 100.0 : 0;
	const double dFees   = positionBefore.dFeesPaid + dCloseFee;

	strCycleDir = strBackupDir + "/" CYCLE_DIR_PREFIX + std::to_string(nCycle);
	std::error_code ec;
	fs::create_directories(strCycleDir, ec);
	if(ec)
		strError = "Backup of cycle " + std::to_string(nCycle) + ": cannot create " + strCycleDir +
		           " (" + ec.message() + "), the cycle files stay in " + strLogDir;

	{
		std::ofstream result(strCycleDir + "/" CYCLE_RESULT_FILE);
		result << "[CYCLE " << nCycle << "]\n"
		       << "open            = " << TimeUtils::Format(nOpenTime) << "\n"
		       << "close           = " << TimeUtils::Format(nTime) << " (" << strReason << ")\n"
		       << "duration_hours  = " << Num((nTime - nOpenTime) / 3600.0, 1) << "\n"
		       << "buys            = " << nBuys << "\n"
		       << "entry_price     = " << Num(dFirstPrice) << "\n"
		       << "avg_price       = " << Num(positionBefore.dAvgPrice) << "\n"
		       << "exit_price      = " << Num(dExitPrice) << "\n"
		       << "capital         = " << Num(dWalletStart) << " -> " << Num(dWalletAfter) << "\n"
		       << "net_growth      = " << Num(dGrowth) << "%\n"
		       << "max_drawdown    = " << Num(dMinEquityPerc) << "%\n"
		       << "max_leverage    = " << Num(dMaxLeverage) << "\n"
		       << "fees            = " << Num(dFees) << "\n"
		       << "funding         = " << Num(positionBefore.dFundingPaid) << "\n";
	}

	{
		std::ofstream total(strTotalFile, std::ios::app);
		total << nCycle << ";" << TimeUtils::Format(nOpenTime) << ";" << TimeUtils::Format(nTime) << ";"
		      << Num(dWalletStart) << ";" << Num(dWalletAfter) << ";" << Num(dGrowth) << ";"
		      << Num(dMinEquityPerc) << ";" << Num(dMaxLeverage) << ";" << nBuys << ";"
		      << Num(dFirstPrice) << ";" << Num(positionBefore.dAvgPrice) << ";" << Num(dExitPrice) << ";"
		      << Num(dFees) << ";" << Num(positionBefore.dFundingPaid) << ";" << strReason << "\n";
		if(!total && strError.empty())
			strError = "Cannot update " + strTotalFile;
	}

	const std::string strSummary = "Cycle " + std::to_string(nCycle) + " closed (" + strReason + "): capital " +
	                               Num(dWalletStart) + " -> " + Num(dWalletAfter) + " (" + Num(dGrowth) + "%), buys " +
	                               std::to_string(nBuys) + ", drawdown max " + Num(dMinEquityPerc) + "%, max leverage " +
	                               Num(dMaxLeverage);
	bOpen = false;
	return strSummary;
}

bool CCycleArchive::MoveLogFiles(std::string& strError)
{
	bool bOk = true;
	std::error_code ec;

	std::vector<fs::path> vFiles;
	for(const fs::directory_entry& e : fs::directory_iterator(strLogDir, ec))
		if(e.is_regular_file(ec))
			vFiles.push_back(e.path());

	for(const fs::path& p : vFiles)
	{
		if(std::find(vKeep.begin(), vKeep.end(), p.lexically_normal().string()) != vKeep.end())
			continue;

		fs::rename(p, fs::path(strCycleDir) / p.filename(), ec);
		if(ec)
		{
			strError = "Backup of cycle " + std::to_string(nCycle) + ": cannot move " + p.string() +
			           " (" + ec.message() + ")";
			bOk = false;
		}
	}

	++nCycle;
	return bOk;
}

CCycleArchive::Snapshot CCycleArchive::GetSnapshot() const
{
	Snapshot s;
	s.bOpen          = bOpen;
	s.nOpenTime      = nOpenTime;
	s.dWalletStart   = dWalletStart;
	s.dFirstPrice    = dFirstPrice;
	s.nBuys          = nBuys;
	s.dMinEquityPerc = dMinEquityPerc;
	s.dMaxLeverage   = dMaxLeverage;
	return s;
}

void CCycleArchive::Restore(const Snapshot& s)
{
	bOpen          = s.bOpen;
	nOpenTime      = s.nOpenTime;
	dWalletStart   = s.dWalletStart;
	dFirstPrice    = s.dFirstPrice;
	nBuys          = s.nBuys;
	dMinEquityPerc = s.dMinEquityPerc;
	dMaxLeverage   = s.dMaxLeverage;
}
