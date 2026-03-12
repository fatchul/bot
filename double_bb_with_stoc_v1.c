//+------------------------------------------------------------------+
//| TrendFollower_MTF_LotManual.mq5                                  |
//+------------------------------------------------------------------+
#property strict

#include <Trade/Trade.mqh>
#include <Trade/PositionInfo.mqh>

CTrade Trade;
CPositionInfo PositionInfo;

//================ INPUT =================

input ENUM_TIMEFRAMES SignalTF = PERIOD_M5;

input double LotSize = 0.01;

input group "=== EQUITY MANAGEMENT ==="
input double InpInitialEquity   = 1000;
input double InpProfitTargetPct = 10.0;
input double InpMaxLossPct      = 10.0;

input group "=== TRADE TARGET ==="
input double InpTakeProfitPips  = 50;

input group "=== TRAILING ==="
input double InpBreakEvenPips = 20;
input double InpTrailingStart = 30;
input double InpTrailingStop  = 15;

input group "=== STOCHASTIC SETTINGS ==="
input int InpStochK       = 10;
input int InpStochD       = 3;
input int InpStochSlowing = 3;
input double InpOversold  = 10;
input double InpOverbought = 90;

input group "=== TRADE SETTINGS ==="
input string InpTradeComment = "TrendFollower";
input int    InpMagicNumber  = 20240101;

//================ GLOBAL =================

int handleStoch;

double gInitialEquity=0;
bool gTradingHalted=false;

datetime gLastBarTime=0;

int gCurrentHour=-1;
int gWinStreak=0;
bool gHourLimitReached=false;

//+------------------------------------------------------------------+
int OnInit()
{
   handleStoch=iStochastic(_Symbol,SignalTF,InpStochK,InpStochD,InpStochSlowing,MODE_SMA,STO_LOWHIGH);

   if(handleStoch==INVALID_HANDLE)
      return INIT_FAILED;

   if(InpInitialEquity>0)
      gInitialEquity=InpInitialEquity;
   else
      gInitialEquity=AccountInfoDouble(ACCOUNT_EQUITY);

   Trade.SetExpertMagicNumber(InpMagicNumber);

   return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
void OnTick()
{
   ManageTrailing();

   if(CheckEquityLimits())
      return;

   ResetHourIfNeeded();

   if(gHourLimitReached)
      return;

   datetime currentBar=iTime(_Symbol,SignalTF,0);

   if(currentBar==gLastBarTime)
      return;

   gLastBarTime=currentBar;

   double stochMain[];
   double stochSignal[];

   ArraySetAsSeries(stochMain,true);
   ArraySetAsSeries(stochSignal,true);

   if(CopyBuffer(handleStoch,0,0,3,stochMain)<3) return;
   if(CopyBuffer(handleStoch,1,0,3,stochSignal)<3) return;

   double close1=iClose(_Symbol,SignalTF,1);
   double open1=iOpen(_Symbol,SignalTF,1);

   double stoch1=stochMain[1];
   double stoch2=stochMain[2];

   double signal1=stochSignal[1];
   double signal2=stochSignal[2];

   bool bullish  = close1>open1;
   bool bearish  = close1<open1;

   // BUY
   bool oversold = (stoch1<InpOversold || stoch2<InpOversold);
   bool crossUp  = (stoch2<=signal2 && stoch1>signal1);

   if(oversold && crossUp && bullish)
   {
      double entry = SymbolInfoDouble(_Symbol,SYMBOL_ASK);

      double sl = entry - (InpTakeProfitPips*10*_Point);
      double tp = entry + (InpTakeProfitPips*10*_Point);

      Trade.Buy(LotSize,_Symbol,0,sl,tp,InpTradeComment);
   }

   // SELL
   bool overbought = (stoch1>InpOverbought || stoch2>InpOverbought);
   bool crossDown  = (stoch2>=signal2 && stoch1<signal1);

   if(overbought && crossDown && bearish)
   {
      double entry = SymbolInfoDouble(_Symbol,SYMBOL_BID);

      double sl = entry + (InpTakeProfitPips*10*_Point);
      double tp = entry - (InpTakeProfitPips*10*_Point);

      Trade.Sell(LotSize,_Symbol,0,sl,tp,InpTradeComment);
   }
}

//+------------------------------------------------------------------+
void ResetHourIfNeeded()
{
   MqlDateTime tm;
   TimeToStruct(TimeCurrent(),tm);

   int hour = tm.hour;

   if(hour!=gCurrentHour)
   {
      gCurrentHour=hour;
      gWinStreak=0;
      gHourLimitReached=false;
   }
}

//+------------------------------------------------------------------+
void ManageTrailing()
{
   double pip = 10*_Point;

   for(int i=PositionsTotal()-1;i>=0;i--)
   {
      if(PositionInfo.SelectByIndex(i))
      {
         if(PositionInfo.Symbol()!=_Symbol) continue;

         ulong ticket = PositionInfo.Ticket();
         double openPrice = PositionInfo.PriceOpen();
         double sl = PositionInfo.StopLoss();
         double tp = PositionInfo.TakeProfit();

         double price;

         if(PositionInfo.PositionType()==POSITION_TYPE_BUY)
            price = SymbolInfoDouble(_Symbol,SYMBOL_BID);
         else
            price = SymbolInfoDouble(_Symbol,SYMBOL_ASK);

         double profitPips=MathAbs(price-openPrice)/(10*_Point);

         if(profitPips>=InpBreakEvenPips)
         {
            Trade.PositionModify(ticket,NormalizeDouble(openPrice,_Digits),tp);
         }

         if(profitPips>=InpTrailingStart)
         {
            double newSL;

            if(PositionInfo.PositionType()==POSITION_TYPE_BUY)
               newSL=price-(InpTrailingStop*10*_Point);
            else
               newSL=price+(InpTrailingStop*10*_Point);

            Trade.PositionModify(ticket,NormalizeDouble(newSL,_Digits),tp);
         }
      }
   }
}

//+------------------------------------------------------------------+
bool CheckEquityLimits()
{
   double equity=AccountInfoDouble(ACCOUNT_EQUITY);

   double changePct=((equity-gInitialEquity)/gInitialEquity)*100.0;

   if(changePct>=InpProfitTargetPct)
   {
      if(!gTradingHalted)
      {
         gTradingHalted=true;
         Alert("Profit target reached");
      }
      return true;
   }

   if(changePct<=-InpMaxLossPct)
   {
      if(!gTradingHalted)
      {
         gTradingHalted=true;
         Alert("Max loss reached");
      }
      return true;
   }

   return false;
}

//+------------------------------------------------------------------+
void OnTradeTransaction(const MqlTradeTransaction& trans,
                        const MqlTradeRequest& request,
                        const MqlTradeResult& result)
{
   if(trans.type==TRADE_TRANSACTION_DEAL_ADD)
   {
      double dealProfit=HistoryDealGetDouble(trans.deal,DEAL_PROFIT);

      if(dealProfit>0)
      {
         gWinStreak++;

         if(gWinStreak>=3)
         {
            gHourLimitReached=true;
            Print("3 wins reached, stop trading this hour");
         }
      }
      else
      {
         gWinStreak=0;
      }
   }
}
//+------------------------------------------------------------------+