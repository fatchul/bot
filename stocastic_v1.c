//+------------------------------------------------------------------+
//|                 Stochastic_RR_BEP_EA.mq5                         |
//+------------------------------------------------------------------+
#property strict

#include <Trade/Trade.mqh>

CTrade trade;

//================ INPUT =================
input ENUM_TIMEFRAMES SignalTF = PERIOD_M5;

input double LotSize = 0.01;
input double RiskPercent = 1.0;
input double RR = 2.0;

input int MagicNumber = 777;

input int Kperiod = 10;
input int Dperiod = 3;
input int Slowing = 3;

input double Overbought = 80;
input double Oversold = 20;

//================ GLOBAL =================
int stochHandle;
double Kbuffer[];
double Dbuffer[];

//+------------------------------------------------------------------+
int OnInit()
{
   stochHandle = iStochastic(_Symbol,SignalTF,Kperiod,Dperiod,Slowing,MODE_SMA,STO_LOWHIGH);

   if(stochHandle==INVALID_HANDLE)
      return(INIT_FAILED);

   ArraySetAsSeries(Kbuffer,true);
   ArraySetAsSeries(Dbuffer,true);

   trade.SetExpertMagicNumber(MagicNumber);

   return(INIT_SUCCEEDED);
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

void OnTick()
{
   if(CopyBuffer(stochHandle,0,0,3,Kbuffer)<=0) return;
   if(CopyBuffer(stochHandle,1,0,3,Dbuffer)<=0) return;

   ManageBreakEven();

   if(PositionSelect(_Symbol)) return;

   double K0 = Kbuffer[0];
   double K1 = Kbuffer[1];

   double D0 = Dbuffer[0];
   double D1 = Dbuffer[1];

   bool crossBuy = (K1 < D1 && K0 > D0);
   bool crossSell = (K1 > D1 && K0 < D0);

   double distance = GetSLdistance();

   //================ BUY =================
   if(K0 < Oversold && crossBuy)
   {
      double price = SymbolInfoDouble(_Symbol,SYMBOL_ASK);

      double sl = price - distance;
      double tp = price + distance*RR;

      trade.Buy(LotSize,_Symbol,price,sl,tp);
   }

   //================ SELL =================
   if(K0 > Overbought && crossSell)
   {
      double price = SymbolInfoDouble(_Symbol,SYMBOL_BID);

      double sl = price + distance;
      double tp = price - distance*RR;

      trade.Sell(LotSize,_Symbol,price,sl,tp);
   }
}
//+------------------------------------------------------------------+