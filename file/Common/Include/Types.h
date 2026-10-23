#pragma once
#include <string>

enum class Mode
{
	Simulated,
	Real
};

enum class MarginMode
{
	Crossed,
	Isolated
};

struct Candle
{
	long long nTime   = 0;
	double    dOpen   = 0;
	double    dHigh   = 0;
	double    dLow    = 0;
	double    dClose  = 0;
	double    dVolume = 0;
};

struct Account
{
	double dWallet    = 0;
	double dEquity    = 0;
	double dAvailable = 0;
};

struct Position
{
	bool   bOpen             = false;
	double dQty              = 0;
	double dAvgPrice         = 0;
	double dMargin           = 0;
	double dLeverage         = 0;
	double dUnrealizedPnL    = 0;
	double dLiquidationPrice = 0;
	double dFeesPaid         = 0;
	double dFundingPaid      = 0;
};

enum class OrderType
{
	Market,
	Limit
};

enum class OrderSide
{
	Buy,
	Close
};

struct OrderResult
{
	bool        bOk        = false;
	double      dFillPrice = 0;
	double      dQty       = 0;
	double      dFee       = 0;
	std::string strError;
	OrderType   eType      = OrderType::Market;
	double      dFeeRate   = 0;
	long long   nOrderId   = 0;
};

struct LimitOrder
{
	long long nId       = 0;
	OrderSide eSide     = OrderSide::Buy;
	double    dPrice    = 0;
	double    dUsdt     = 0;
	double    dLeverage = 0;
	double    dQty      = 0;
	double    dReserved = 0;
	long long nTime     = 0;
};

struct Fill
{
	long long nOrderId = 0;
	OrderSide eSide    = OrderSide::Buy;
	OrderType eType    = OrderType::Limit;
	long long nTime    = 0;
	double    dPrice   = 0;
	double    dQty     = 0;
	double    dFee     = 0;
	double    dFeeRate = 0;
	double    dWalletBefore = 0;
};

struct ExchangeRules
{
	MarginMode eMarginMode        = MarginMode::Isolated;
	double     dFeeTaker          = 0;
	double     dFeeMaker          = 0;
	double     dSlippage          = 0;
	double     dMaintenanceMargin = 0;
	double     dQtyStep           = 0;
	double     dMinQty            = 0;
	double     dMinNotional       = 0;
};
