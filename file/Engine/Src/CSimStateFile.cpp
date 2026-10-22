#include "CSimStateFile.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>

#include "Settings.h"

#define STATE_RUN			"RUN"
#define STATE_ACCOUNT		"ACCOUNT"
#define STATE_POSITION		"POSITION"
#define STATE_STATS			"STATS"
#define STATE_CYCLE			"CYCLE"

namespace
{
	class CStateReader
	{
	public:
		explicit CStateReader(const std::string& strFile): ini(strFile, false), bOk(true) {}

		bool Valid() const { return ini.IsValid(); }
		bool Ok() const { return bOk; }
		const std::string& Missing() const { return strMissing; }

		void Get(const char* pszSection, const char* pszKey, double& d)
		{
			if(!ini.Get(pszSection, pszKey, d))
				_Fail(pszSection, pszKey);
		}
		void Get(const char* pszSection, const char* pszKey, long long& n)
		{
			std::string s;
			if(!ini.Get(pszSection, pszKey, s))
			{
				_Fail(pszSection, pszKey);
				return;
			}
			try { n = std::stoll(s); }
			catch(...) { _Fail(pszSection, pszKey); }
		}
		void Get(const char* pszSection, const char* pszKey, int& n)
		{
			long long v = 0;
			Get(pszSection, pszKey, v);
			n = static_cast<int>(v);
		}
		void Get(const char* pszSection, const char* pszKey, bool& b)
		{
			long long v = 0;
			Get(pszSection, pszKey, v);
			b = v != 0;
		}
		void Get(const char* pszSection, const char* pszKey, std::string& s)
		{
			if(!ini.Get(pszSection, pszKey, s))
				_Fail(pszSection, pszKey);
		}

	private:
		void _Fail(const char* pszSection, const char* pszKey)
		{
			bOk = false;
			strMissing += std::string(strMissing.empty() ? "" : ", ") + "[" + pszSection + "] " + pszKey;
		}

		CReader     ini;
		bool        bOk;
		std::string strMissing;
	};
}

bool CSimStateFile::Save(const std::string& strFile, const SimState& s, std::string& strError)
{
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::setprecision(17);

	const Position& p = s.exchange.position;
	const CSimExchange::Stats& st = s.exchange.stats;
	const CCycleArchive::Snapshot& c = s.cycle;

	out << "# SXE simulation state: written by the engine, do not edit\n"
	    << "[" STATE_RUN "]\n"
	    << "data_file = "     << s.strDataFile  << "\n"
	    << "strategy = "      << s.strStrategy  << "\n"
	    << "candle_time = "   << s.nCandleTime  << "\n"
	    << "next_tick = "     << s.nNextTick    << "\n"
	    << "step = "          << s.nStep        << "\n"
	    << "peak_equity = "   << s.dPeakEquity  << "\n"
	    << "max_drawdown = "  << s.dMaxDrawdown << "\n"
	    << "\n[" STATE_ACCOUNT "]\n"
	    << "time = "          << s.exchange.nTime        << "\n"
	    << "price = "         << s.exchange.dPrice       << "\n"
	    << "wallet = "        << s.exchange.dWallet      << "\n"
	    << "funding_slot = "  << s.exchange.nFundingSlot << "\n"
	    << "\n[" STATE_POSITION "]\n"
	    << "open = "          << (p.bOpen ? 1 : 0)  << "\n"
	    << "qty = "           << p.dQty             << "\n"
	    << "avg_price = "     << p.dAvgPrice        << "\n"
	    << "margin = "        << p.dMargin          << "\n"
	    << "leverage = "      << p.dLeverage        << "\n"
	    << "fees_paid = "     << p.dFeesPaid        << "\n"
	    << "funding_paid = "  << p.dFundingPaid     << "\n"
	    << "\n[" STATE_STATS "]\n"
	    << "orders = "        << st.nOrders         << "\n"
	    << "rejected = "      << st.nRejected       << "\n"
	    << "closed = "        << st.nClosed         << "\n"
	    << "wins = "          << st.nWins           << "\n"
	    << "liquidations = "  << st.nLiquidations   << "\n"
	    << "fees_paid = "     << st.dFeesPaid       << "\n"
	    << "funding_paid = "  << st.dFundingPaid    << "\n"
	    << "realized_pnl = "  << st.dRealizedPnL    << "\n"
	    << "\n[" STATE_CYCLE "]\n"
	    << "open = "          << (c.bOpen ? 1 : 0)  << "\n"
	    << "open_time = "     << c.nOpenTime        << "\n"
	    << "wallet_start = "  << c.dWalletStart     << "\n"
	    << "first_price = "   << c.dFirstPrice      << "\n"
	    << "buys = "          << c.nBuys            << "\n"
	    << "min_equity = "    << c.dMinEquityPerc   << "\n"
	    << "max_leverage = "  << c.dMaxLeverage     << "\n";

	const std::string strTmp = strFile + ".tmp";
	{
		std::ofstream file(strTmp, std::ios::trunc);
		file << out.str();
		file.flush();
		if(!file)
		{
			strError = "Cannot write " + strTmp + ": simulation state not saved";
			return false;
		}
	}

	std::error_code ec;
	std::filesystem::rename(strTmp, strFile, ec);
	if(ec)
	{
		strError = "Cannot update " + strFile + " (" + ec.message() + "): simulation state not saved";
		std::filesystem::remove(strTmp, ec);
		return false;
	}
	return true;
}

bool CSimStateFile::Load(const std::string& strFile, SimState& s, std::string& strError)
{
	CStateReader in(strFile);
	if(!in.Valid())
	{
		strError = "Simulation state cannot be read: " + strFile;
		return false;
	}

	Position& p = s.exchange.position;
	CSimExchange::Stats& st = s.exchange.stats;
	CCycleArchive::Snapshot& c = s.cycle;

	in.Get(STATE_RUN, "data_file", s.strDataFile);
	in.Get(STATE_RUN, "strategy", s.strStrategy);
	in.Get(STATE_RUN, "candle_time", s.nCandleTime);
	in.Get(STATE_RUN, "next_tick", s.nNextTick);
	in.Get(STATE_RUN, "step", s.nStep);
	in.Get(STATE_RUN, "peak_equity", s.dPeakEquity);
	in.Get(STATE_RUN, "max_drawdown", s.dMaxDrawdown);

	in.Get(STATE_ACCOUNT, "time", s.exchange.nTime);
	in.Get(STATE_ACCOUNT, "price", s.exchange.dPrice);
	in.Get(STATE_ACCOUNT, "wallet", s.exchange.dWallet);
	in.Get(STATE_ACCOUNT, "funding_slot", s.exchange.nFundingSlot);

	in.Get(STATE_POSITION, "open", p.bOpen);
	in.Get(STATE_POSITION, "qty", p.dQty);
	in.Get(STATE_POSITION, "avg_price", p.dAvgPrice);
	in.Get(STATE_POSITION, "margin", p.dMargin);
	in.Get(STATE_POSITION, "leverage", p.dLeverage);
	in.Get(STATE_POSITION, "fees_paid", p.dFeesPaid);
	in.Get(STATE_POSITION, "funding_paid", p.dFundingPaid);

	in.Get(STATE_STATS, "orders", st.nOrders);
	in.Get(STATE_STATS, "rejected", st.nRejected);
	in.Get(STATE_STATS, "closed", st.nClosed);
	in.Get(STATE_STATS, "wins", st.nWins);
	in.Get(STATE_STATS, "liquidations", st.nLiquidations);
	in.Get(STATE_STATS, "fees_paid", st.dFeesPaid);
	in.Get(STATE_STATS, "funding_paid", st.dFundingPaid);
	in.Get(STATE_STATS, "realized_pnl", st.dRealizedPnL);

	in.Get(STATE_CYCLE, "open", c.bOpen);
	in.Get(STATE_CYCLE, "open_time", c.nOpenTime);
	in.Get(STATE_CYCLE, "wallet_start", c.dWalletStart);
	in.Get(STATE_CYCLE, "first_price", c.dFirstPrice);
	in.Get(STATE_CYCLE, "buys", c.nBuys);
	in.Get(STATE_CYCLE, "min_equity", c.dMinEquityPerc);
	in.Get(STATE_CYCLE, "max_leverage", c.dMaxLeverage);

	if(!in.Ok())
	{
		strError = "Incomplete simulation state in " + strFile + " (" + in.Missing() + ")";
		return false;
	}
	return true;
}
