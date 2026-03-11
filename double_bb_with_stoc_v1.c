#property strict

#include <Trade/Trade.mqh>

CTrade trade;

// INPUT PARAMETERS
input double LotSize = 0.01;
input double RiskReward = 1.5;

input int EMA_Period = 200;

input int BB_Period = 25;
input double BB_Deviation = 3.0;

input int Stoch_K = 10;
input int Stoch_D = 3;
input int Stoch_Slow = 3;

input double Overbought = 80;
input double Oversold = 20;

input double MomentumBodyPercent = 0.6;
input double LiquidityWickPercent = 0.4;

input double MaxSpread = 300;

input int EMASlopeLookback = 10;
input double MinEMASlope = 5;

input double MinBBWidth = 200;

input int SessionStart = 14;
input int SessionEnd = 23;

input int MagicNumber = 777;


// INDICATOR HANDLES
int emaHandle;
int bbHandle;
int stochHandle;


// STATE MACHINE
enum EAState
{
   WAIT_LIQUIDITY,
   WAIT_STOCH,
   WAIT_MOMENTUM
};

EAState currentState = WAIT_LIQUIDITY;


// NEW BAR TRACKER
datetime lastBar = 0;


//+------------------------------------------------------------------+
//| Expert initialization                                            |
//+------------------------------------------------------------------+
int OnInit()
{

   emaHandle = iMA(_Symbol,_Period,EMA_Period,0,MODE_EMA,PRICE_CLOSE);

   bbHandle = iBands(_Symbol,_Period,BB_Period,0,BB_Deviation,PRICE_CLOSE);

   stochHandle = iStochastic(_Symbol,_Period,Stoch_K,Stoch_D,Stoch_Slow,MODE_SMA,STO_LOWHIGH);

   if(emaHandle == INVALID_HANDLE ||
      bbHandle == INVALID_HANDLE ||
      stochHandle == INVALID_HANDLE)
   {
      Print("Indicator handle error");
      return(INIT_FAILED);
   }

   trade.SetExpertMagicNumber(MagicNumber);

   return(INIT_SUCCEEDED);
}


//+------------------------------------------------------------------+
//| New candle detection                                             |
//+------------------------------------------------------------------+
bool IsNewBar()
{
   datetime currentBar = iTime(_Symbol,_Period,0);

   if(currentBar != lastBar)
   {
      lastBar = currentBar;
      return true;
   }

   return false;
}


//+------------------------------------------------------------------+
//| Spread filter                                                    |
//+------------------------------------------------------------------+
bool CheckSpread()
{
   double spread = (SymbolInfoDouble(_Symbol,SYMBOL_ASK) - SymbolInfoDouble(_Symbol,SYMBOL_BID))/_Point;

   if(spread <= MaxSpread)
      return true;

   return false;
}


//+------------------------------------------------------------------+
//| Session filter                                                   |
//+------------------------------------------------------------------+
bool CheckSession()
{
   MqlDateTime time;
   TimeCurrent(time);

   if(time.hour >= SessionStart && time.hour <= SessionEnd)
      return true;

   return false;
}


//+------------------------------------------------------------------+
//| EMA Trend                                                        |
//+------------------------------------------------------------------+
bool TrendBuy()
{
   double ema[];

   CopyBuffer(emaHandle,0,0,2,ema);

   double close = iClose(_Symbol,_Period,1);

   if(close > ema[1])
      return true;

   return false;
}

bool TrendSell()
{
   double ema[];

   CopyBuffer(emaHandle,0,0,2,ema);

   double close = iClose(_Symbol,_Period,1);

   if(close < ema[1])
      return true;

   return false;
}


//+------------------------------------------------------------------+
//| Liquidity Sweep                                                  |
//+------------------------------------------------------------------+
bool CheckLiquidityBuy()
{
   double high = iHigh(_Symbol,_Period,1);
   double low = iLow(_Symbol,_Period,1);
   double open = iOpen(_Symbol,_Period,1);
   double close = iClose(_Symbol,_Period,1);

   double range = high - low;
   double lowerWick = MathMin(open,close) - low;

   if(range <= 0) return false;

   if((lowerWick/range) >= LiquidityWickPercent)
      return true;

   return false;
}


bool CheckLiquiditySell()
{
   double high = iHigh(_Symbol,_Period,1);
   double low = iLow(_Symbol,_Period,1);
   double open = iOpen(_Symbol,_Period,1);
   double close = iClose(_Symbol,_Period,1);

   double range = high - low;
   double upperWick = high - MathMax(open,close);

   if(range <= 0) return false;

   if((upperWick/range) >= LiquidityWickPercent)
      return true;

   return false;
}


//+------------------------------------------------------------------+
//| Momentum candle                                                  |
//+------------------------------------------------------------------+
bool CheckMomentumBuy()
{
   double open = iOpen(_Symbol,_Period,1);
   double close = iClose(_Symbol,_Period,1);
   double high = iHigh(_Symbol,_Period,1);
   double low = iLow(_Symbol,_Period,1);

   double range = high - low;
   double body = MathAbs(close-open);

   if(close > open && (body/range) >= MomentumBodyPercent)
      return true;

   return false;
}

bool CheckMomentumSell()
{
   double open = iOpen(_Symbol,_Period,1);
   double close = iClose(_Symbol,_Period,1);
   double high = iHigh(_Symbol,_Period,1);
   double low = iLow(_Symbol,_Period,1);

   double range = high - low;
   double body = MathAbs(close-open);

   if(close < open && (body/range) >= MomentumBodyPercent)
      return true;

   return false;
}


//+------------------------------------------------------------------+
//| Stochastic Cross                                                 |
//+------------------------------------------------------------------+
bool StochCrossBuy()
{
   double K[2];
   double D[2];

   CopyBuffer(stochHandle,0,0,2,K);
   CopyBuffer(stochHandle,1,0,2,D);

   if(K[1] < D[1] && K[0] > D[0] && K[0] < Oversold)
      return true;

   return false;
}


bool StochCrossSell()
{
   double K[2];
   double D[2];

   CopyBuffer(stochHandle,0,0,2,K);
   CopyBuffer(stochHandle,1,0,2,D);

   if(K[1] > D[1] && K[0] < D[0] && K[0] > Overbought)
      return true;

   return false;
}


//+------------------------------------------------------------------+
//| Entry                                                            |
//+------------------------------------------------------------------+
void OpenBuy()
{
   double price = SymbolInfoDouble(_Symbol,SYMBOL_ASK);

   trade.Buy(LotSize,_Symbol,price,0,0);
}

void OpenSell()
{
   double price = SymbolInfoDouble(_Symbol,SYMBOL_BID);

   trade.Sell(LotSize,_Symbol,price,0,0);
}


//+------------------------------------------------------------------+
//| Expert tick                                                      |
//+------------------------------------------------------------------+
void OnTick()
{

   if(!IsNewBar())
      return;

   if(PositionsTotal() > 0)
      return;

   if(!CheckSpread())
      return;

   if(!CheckSession())
      return;


   // BUY PIPELINE
   if(TrendBuy())
   {

      if(currentState == WAIT_LIQUIDITY)
      {
         if(CheckLiquidityBuy())
            currentState = WAIT_STOCH;
      }

      else if(currentState == WAIT_STOCH)
      {
         if(StochCrossBuy())
            currentState = WAIT_MOMENTUM;
      }

      else if(currentState == WAIT_MOMENTUM)
      {
         if(CheckMomentumBuy())
         {
            OpenBuy();
            currentState = WAIT_LIQUIDITY;
         }
      }

   }


   // SELL PIPELINE
   if(TrendSell())
   {

      if(currentState == WAIT_LIQUIDITY)
      {
         if(CheckLiquiditySell())
            currentState = WAIT_STOCH;
      }

      else if(currentState == WAIT_STOCH)
      {
         if(StochCrossSell())
            currentState = WAIT_MOMENTUM;
      }

      else if(currentState == WAIT_MOMENTUM)
      {
         if(CheckMomentumSell())
         {
            OpenSell();
            currentState = WAIT_LIQUIDITY;
         }
      }

   }

}