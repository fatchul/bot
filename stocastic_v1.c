//+------------------------------------------------------------------+
//|                 Stochastic_RR_BEP_EA.mq5                         |
//+------------------------------------------------------------------+
#property strict

#include <Trade/Trade.mqh>

CTrade trade;

//================ INPUT =================
input ENUM_TIMEFRAMES SignalTF = PERIOD_M5;

input double DailyTargetPercent = 40.0;

input double LotSize = 0.01;
input double RiskPercent = 1.0;
input double RR = 2.0;
input bool UseStochExit = true;

input int MagicNumber = 777;

input int Kperiod = 10;
input int Dperiod = 3;
input int Slowing = 3;

input double Overbought = 80;
input double Oversold = 20;

// EMA FILTER
input bool UseEMAFilter = true;
input int  EMAPeriod = 200;

// Consecutive Loss Limit
input int MaxConsecutiveLoss = 3;

input string TelegramBotToken = "8795364053:AAEHpkMuUsu2xeKTHqg1_nclACDuvlxoyQ4";
input string TelegramChatID   = "-1003848904572";

//================ GLOBAL =================
int stochHandle;
double Kbuffer[];
double Dbuffer[];

int emaHandle;
double EMABuffer[];

double StartEquityToday;
int CurrentDay;

int ConsecutiveLossCount = 0;
ulong LastProcessedDeal = 0;

//+------------------------------------------------------------------+
int OnInit()
{
   stochHandle = iStochastic(_Symbol,SignalTF,Kperiod,Dperiod,Slowing,MODE_SMA,STO_LOWHIGH);

   if(stochHandle==INVALID_HANDLE)
      return(INIT_FAILED);

   ArraySetAsSeries(Kbuffer,true);
   ArraySetAsSeries(Dbuffer,true);

   trade.SetExpertMagicNumber(MagicNumber);

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

void ManageStochasticExit()
{
   if(!UseStochExit)
      return;

   if(!PositionSelect(_Symbol))
      return;

   if(PositionGetInteger(POSITION_MAGIC)!=MagicNumber)
      return;

   if(CopyBuffer(stochHandle,0,0,3,Kbuffer)<=0) return;
   if(CopyBuffer(stochHandle,1,0,3,Dbuffer)<=0) return;

   double K0 = Kbuffer[0];
   double K1 = Kbuffer[1];

   double D0 = Dbuffer[0];
   double D1 = Dbuffer[1];

   bool crossBuy = (K1 < D1 && K0 > D0);
   bool crossSell = (K1 > D1 && K0 < D0);

   long type = PositionGetInteger(POSITION_TYPE);

   if(type==POSITION_TYPE_BUY && crossSell)
   {
      trade.PositionClose(_Symbol);
   }

   if(type==POSITION_TYPE_SELL && crossBuy)
   {
      trade.PositionClose(_Symbol);
   }
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

      ConsecutiveLossCount = 0;
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

void CheckLastClosedTrade()
{
   HistorySelect(0,TimeCurrent());

   int deals = HistoryDealsTotal();

   if(deals==0) return;

   ulong ticket = HistoryDealGetTicket(deals-1);

   if(ticket == LastProcessedDeal)
      return;

   LastProcessedDeal = ticket;

   if(HistoryDealGetInteger(ticket,DEAL_MAGIC) != MagicNumber)
      return;

   if(HistoryDealGetString(ticket,DEAL_SYMBOL) != _Symbol)
      return;

   double profit = HistoryDealGetDouble(ticket,DEAL_PROFIT);

   int entry = (int)HistoryDealGetInteger(ticket,DEAL_ENTRY);

   if(entry != DEAL_ENTRY_OUT)
      return;

   if(profit < 0)
      ConsecutiveLossCount++;
   else
      ConsecutiveLossCount = 0;
}

//+------------------------------------------------------------------+

bool MaxLossReached()
{
   if(ConsecutiveLossCount >= MaxConsecutiveLoss)
      return true;

   return false;
}

//+------------------------------------------------------------------+

void OnTick()
{
   ResetDailyEquity();

   CheckLastClosedTrade();

   ManageBreakEven();
   ManageStochasticExit();
   
   if(MaxLossReached()) return;

   if(DailyTargetReached()) return;

   if(HaveOpenPosition()) return;

   if(CopyBuffer(stochHandle,0,0,3,Kbuffer)<=0) return;
   if(CopyBuffer(stochHandle,1,0,3,Dbuffer)<=0) return;

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
      
      if(trade.Buy(LotSize,_Symbol,price,sl,tp))
      {
        string msg = "BUY " + _Symbol +
             "\nTimeframe: " + EnumToString(SignalTF) +
             "\nPrice: " + DoubleToString(price,_Digits) +
             "\nSL: " + DoubleToString(sl,_Digits) +
             "\nTP: " + DoubleToString(tp,_Digits);
      
        SendTelegramMessage(msg);
      }
   }

   //================ SELL =================
   if(K0 > Overbought && crossSell && TrendSellAllowed())
   {
      double price = SymbolInfoDouble(_Symbol,SYMBOL_BID);

      double sl = price + distance;
      double tp = price - distance*RR;

      trade.Sell(LotSize,_Symbol,price,sl,tp);
      if(trade.Sell(LotSize,_Symbol,price,sl,tp))
      {
        string msg = "SELL " + _Symbol +
             "\nTimeframe: " + EnumToString(SignalTF) +
             "\nPrice: " + DoubleToString(price,_Digits) +
             "\nSL: " + DoubleToString(sl,_Digits) +
             "\nTP: " + DoubleToString(tp,_Digits);

        SendTelegramMessage(msg);
      }
   }
}

string UrlEncode(string text)
{
   string encoded = "";
   uchar c;

   for(int i=0; i<StringLen(text); i++)
   {
      c = (uchar)StringGetCharacter(text, i);

      // karakter aman
      if(
         (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~'
      )
      {
         encoded += CharToString(c);
      }
      else if(c == ' ')
      {
         encoded += "%20";
      }
      else
      {
         encoded += StringFormat("%%%02X", c);
      }
   }

   return encoded;
}

void SendTelegramMessage(string text)
{
   if(StringLen(TelegramBotToken)==0 || StringLen(TelegramChatID)==0)
      return;

   string url = "https://api.telegram.org/bot"+TelegramBotToken+"/sendMessage";

   string data = "chat_id="+TelegramChatID+"&text="+UrlEncode(text);

   char result[];
   char post[];

   StringToCharArray(data, post);

   string headers = "Content-Type: application/x-www-form-urlencoded\r\n";

   int res = WebRequest("POST", url, headers, 5000, post, result, headers);

   if(res == -1)
   {
      Print("Telegram Error: ", GetLastError());
   }
   else
   {
      Print("Telegram sent: ", text);
   }
}

bool LiquiditySweepBuy()
{
   double low1 = iLow(_Symbol, SignalTF, 1);
   double low2 = iLow(_Symbol, SignalTF, 2);

   double open = iOpen(_Symbol, SignalTF, 1);
   double close = iClose(_Symbol, SignalTF, 1);
   double low = iLow(_Symbol, SignalTF, 1);

   // sweep: low sekarang lebih rendah dari sebelumnya
   bool sweep = low < low2;

   // rejection: close kembali naik + wick bawah panjang
   double body = MathAbs(close - open);
   double lowerWick = MathMin(open,close) - low;

   bool rejection = lowerWick > body;

   return (sweep && rejection);
}

//-------------------------------------

bool LiquiditySweepSell()
{
   double high1 = iHigh(_Symbol, SignalTF, 1);
   double high2 = iHigh(_Symbol, SignalTF, 2);

   double open = iOpen(_Symbol, SignalTF, 1);
   double close = iClose(_Symbol, SignalTF, 1);
   double high = iHigh(_Symbol, SignalTF, 1);

   bool sweep = high > high2;

   double body = MathAbs(close - open);
   double upperWick = high - MathMax(open,close);

   bool rejection = upperWick > body;

   return (sweep && rejection);
}
//+------------------------------------------------------------------+