//+------------------------------------------------------------------+
//|                 Stochastic_RR_BEP_EA.mq5                         |
//+------------------------------------------------------------------+
#property strict

#include <Trade/Trade.mqh>

CTrade trade;

//================ INPUT =================
input ENUM_TIMEFRAMES SignalTF = PERIOD_M5;

input double DailyTargetPercent = 40.0; // max profit per hari (%)

input double LotSize = 0.01;
input double RiskPercent = 1.0;
input double RR = 2.0;

input int MagicNumber = 777;

input int Kperiod = 10;
input int Dperiod = 3;
input int Slowing = 3;

input double Overbought = 80;
input double Oversold = 20;

// ==== EMA FILTER ====
input bool UseEMAFilter = true;
input int  EMAPeriod = 200;

//================ GLOBAL =================
int stochHandle;
double Kbuffer[];
double Dbuffer[];

int emaHandle;
double EMABuffer[];

double StartEquityToday;
int CurrentDay;

//+------------------------------------------------------------------+
int OnInit()
{
   stochHandle = iStochastic(_Symbol,SignalTF,Kperiod,Dperiod,Slowing,MODE_SMA,STO_LOWHIGH);

   if(stochHandle==INVALID_HANDLE)
      return(INIT_FAILED);

   ArraySetAsSeries(Kbuffer,true);
   ArraySetAsSeries(Dbuffer,true);

   trade.SetExpertMagicNumber(MagicNumber);

   // EMA Handle
   if(UseEMAFilter)
   {
      emaHandle = iMA(_Symbol,SignalTF,EMAPeriod,0,MODE_EMA,PRICE_CLOSE);

      if(emaHandle==INVALID_HANDLE)
         return(INIT_FAILED);

      ArraySetAsSeries(EMABuffer,true);
   }

   StartEquityToday = AccountInfoDouble(ACCOUNT_EQUITY);

   MqlDateTime tm;
   TimeToStruct(TimeCurrent(),tm);
   CurrentDay = tm.day;

   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+

void ResetDailyEquity()
{
   MqlDateTime tm;
   TimeToStruct(TimeCurrent(),tm);

   int today = tm.day;

   if(today != CurrentDay)
   {
      CurrentDay = today;
      StartEquityToday = AccountInfoDouble(ACCOUNT_EQUITY);
   }
}

//+------------------------------------------------------------------+

bool DailyTargetReached()
{
   double equity = AccountInfoDouble(ACCOUNT_EQUITY);

   double profitPercent = ((equity - StartEquityToday) / StartEquityToday) * 100.0;

   if(profitPercent >= DailyTargetPercent)
      return true;

   return false;
}

//+------------------------------------------------------------------+

double GetSLdistance()
{
   double equity = AccountInfoDouble(ACCOUNT_EQUITY);
   double riskMoney = equity * RiskPercent / 100.0;

   double tickvalue = SymbolInfoDouble(_Symbol,SYMBOL_TRADE_TICK_VALUE);

   double distance = riskMoney/(LotSize*tickvalue);

   return distance*_Point;
}

//+------------------------------------------------------------------+

bool TrendBuyAllowed()
{
   if(!UseEMAFilter)
      return true;

   if(CopyBuffer(emaHandle,0,0,1,EMABuffer)<=0)
      return false;

   double price = SymbolInfoDouble(_Symbol,SYMBOL_BID);

   if(price > EMABuffer[0])
      return true;

   return false;
}

//+------------------------------------------------------------------+

bool TrendSellAllowed()
{
   if(!UseEMAFilter)
      return true;

   if(CopyBuffer(emaHandle,0,0,1,EMABuffer)<=0)
      return false;

   double price = SymbolInfoDouble(_Symbol,SYMBOL_BID);

   if(price < EMABuffer[0])
      return true;

   return false;
}

//+------------------------------------------------------------------+

void ManageBreakEven()
{
   if(!PositionSelect(_Symbol)) return;

   if(PositionGetInteger(POSITION_MAGIC)!=MagicNumber) return;

   double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
   double sl = PositionGetDouble(POSITION_SL);
   double tp = PositionGetDouble(POSITION_TP);

   double distance = MathAbs(openPrice-sl);

   long type = PositionGetInteger(POSITION_TYPE);

   if(type==POSITION_TYPE_BUY)
   {
      double price = SymbolInfoDouble(_Symbol,SYMBOL_BID);

      if(price-openPrice >= distance && sl < openPrice)
      {
         trade.PositionModify(_Symbol,openPrice,tp);
      }
   }

   if(type==POSITION_TYPE_SELL)
   {
      double price = SymbolInfoDouble(_Symbol,SYMBOL_ASK);

      if(openPrice-price >= distance && sl > openPrice)
      {
         trade.PositionModify(_Symbol,openPrice,tp);
      }
   }
}

//+------------------------------------------------------------------+

bool HaveOpenPosition()
{
   for(int i=PositionsTotal()-1;i>=0;i--)
   {
      ulong ticket = PositionGetTicket(i);

      if(!PositionSelectByTicket(ticket))
         continue;

      if(PositionGetInteger(POSITION_MAGIC)==MagicNumber &&
         PositionGetString(POSITION_SYMBOL)==_Symbol)
         return true;
   }

   return false;
}

//+------------------------------------------------------------------+

void OnTick()
{
   if(CopyBuffer(stochHandle,0,0,3,Kbuffer)<=0) return;
   if(CopyBuffer(stochHandle,1,0,3,Dbuffer)<=0) return;

   ResetDailyEquity();

   if(HaveOpenPosition()) return;

   if(DailyTargetReached()) return;

   ManageBreakEven();

   double K0 = Kbuffer[0];
   double K1 = Kbuffer[1];

   double D0 = Dbuffer[0];
   double D1 = Dbuffer[1];

   bool crossBuy = (K1 < D1 && K0 > D0);
   bool crossSell = (K1 > D1 && K0 < D0);

   double distance = GetSLdistance();

   //================ BUY =================
   if(K0 < Oversold && crossBuy && TrendBuyAllowed())
   {
      double price = SymbolInfoDouble(_Symbol,SYMBOL_ASK);

      double sl = price - distance;
      double tp = price + distance*RR;

      trade.Buy(LotSize,_Symbol,price,sl,tp);
   }

   //================ SELL =================
   if(K0 > Overbought && crossSell && TrendSellAllowed())
   {
      double price = SymbolInfoDouble(_Symbol,SYMBOL_BID);

      double sl = price + distance;
      double tp = price - distance*RR;

      trade.Sell(LotSize,_Symbol,price,sl,tp);
   }
}
//+------------------------------------------------------------------+