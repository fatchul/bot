//+------------------------------------------------------------------+
//|                 Stochastic_RR_BEP_EA.mq5                         |
//|  Fixed: double entry, BEP flag, magic filter, HistorySelect,     |
//|  SL normalization, LiquiditySweep integrated, chart comment      |
//+------------------------------------------------------------------+
#property strict

#include <Trade/Trade.mqh>

CTrade trade;

//================ INPUT =================
input ENUM_TIMEFRAMES SignalTF           = PERIOD_M5;

input double DailyTargetPercent         = 40.0;

input double LotSize                    = 0.01;
input double RiskPercent                = 1.0;
input double RR                         = 2.0;
input bool   UseStochExit               = true;

input int    MagicNumber                = 777;

input int    Kperiod                    = 10;
input int    Dperiod                    = 3;
input int    Slowing                    = 3;

input double Overbought                 = 90;
input double Oversold                   = 10;

// EMA FILTER
input bool   UseEMAFilter               = true;
input int    EMAPeriod                  = 200;

// Liquidity Sweep Filter
input bool   UseLiquiditySweep         = true;

// Consecutive Loss Limit
input int    MaxConsecutiveLoss         = 3;

input string TelegramBotToken           = "8795364053:AAEHpkMuUsu2xeKTHqg1_nclACDuvlxoyQ4";
input string TelegramChatID             = "-1003848904572";

//================ GLOBAL =================
int    stochHandle;
double Kbuffer[];
double Dbuffer[];

int    emaHandle;
double EMABuffer[];

double StartEquityToday;
int    CurrentDay;

int    ConsecutiveLossCount  = 0;
ulong  LastProcessedDeal     = 0;

// FIX #6: flag agar BEP tidak di-apply ulang
bool   BEPApplied            = false;
ulong  BEPTicket             = 0;

//+------------------------------------------------------------------+
int OnInit()
{
   stochHandle = iStochastic(_Symbol, SignalTF, Kperiod, Dperiod, Slowing, MODE_SMA, STO_LOWHIGH);
   if(stochHandle == INVALID_HANDLE)
      return(INIT_FAILED);

   ArraySetAsSeries(Kbuffer, true);
   ArraySetAsSeries(Dbuffer, true);

   trade.SetExpertMagicNumber(MagicNumber);

   if(UseEMAFilter)
   {
      emaHandle = iMA(_Symbol, SignalTF, EMAPeriod, 0, MODE_EMA, PRICE_CLOSE);
      if(emaHandle == INVALID_HANDLE)
         return(INIT_FAILED);

      ArraySetAsSeries(EMABuffer, true);
   }

   StartEquityToday = AccountInfoDouble(ACCOUNT_EQUITY);

   MqlDateTime tm;
   TimeToStruct(TimeCurrent(), tm);
   CurrentDay = tm.day;

   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   Comment("");
}

//+------------------------------------------------------------------+
// FIX #7: helper cari posisi by magic+symbol, return ticket atau 0
ulong FindPositionTicket()
{
   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong ticket = PositionGetTicket(i);
      if(!PositionSelectByTicket(ticket))
         continue;
      if(PositionGetInteger(POSITION_MAGIC) == MagicNumber &&
         PositionGetString(POSITION_SYMBOL) == _Symbol)
         return ticket;
   }
   return 0;
}

//+------------------------------------------------------------------+
bool HaveOpenPosition()
{
   return (FindPositionTicket() != 0);
}

//+------------------------------------------------------------------+
void ManageStochasticExit()
{
   if(!UseStochExit)
      return;

   ulong ticket = FindPositionTicket();
   if(ticket == 0)
      return;

   if(!PositionSelectByTicket(ticket))
      return;

   if(CopyBuffer(stochHandle, 0, 0, 3, Kbuffer) <= 0) return;
   if(CopyBuffer(stochHandle, 1, 0, 3, Dbuffer) <= 0) return;

   double K0 = Kbuffer[0], K1 = Kbuffer[1];
   double D0 = Dbuffer[0], D1 = Dbuffer[1];

   bool crossBuy  = (K1 < D1 && K0 > D0);
   bool crossSell = (K1 > D1 && K0 < D0);

   long type = PositionGetInteger(POSITION_TYPE);

   if(type == POSITION_TYPE_BUY && crossSell)
   {
      if(trade.PositionClose(_Symbol))
         BEPApplied = false;
   }

   if(type == POSITION_TYPE_SELL && crossBuy)
   {
      if(trade.PositionClose(_Symbol))
         BEPApplied = false;
   }
}

//+------------------------------------------------------------------+
void ResetDailyEquity()
{
   MqlDateTime tm;
   TimeToStruct(TimeCurrent(), tm);
   int today = tm.day;

   if(today != CurrentDay)
   {
      CurrentDay           = today;
      StartEquityToday     = AccountInfoDouble(ACCOUNT_EQUITY);
      ConsecutiveLossCount = 0;
      BEPApplied           = false;

      // Notifikasi reset harian
      SendTelegramMessage("📅 Daily reset\nSymbol: " + _Symbol +
                          "\nEquity: " + DoubleToString(AccountInfoDouble(ACCOUNT_EQUITY), 2));
   }
}

//+------------------------------------------------------------------+
bool DailyTargetReached()
{
   double equity        = AccountInfoDouble(ACCOUNT_EQUITY);
   double profitPercent = ((equity - StartEquityToday) / StartEquityToday) * 100.0;
   return (profitPercent >= DailyTargetPercent);
}

//+------------------------------------------------------------------+
// FIX #3: SL distance dinormalisasi ke tick size
double GetSLdistance()
{
   double equity    = AccountInfoDouble(ACCOUNT_EQUITY);
   double riskMoney = equity * RiskPercent / 100.0;
   double tickValue = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_VALUE);
   double tickSize  = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);

   if(tickValue <= 0 || tickSize <= 0)
      return 0;

   double distance  = (riskMoney / (LotSize * tickValue)) * tickSize;

   // Normalisasi ke kelipatan tickSize
   distance = MathRound(distance / tickSize) * tickSize;

   return distance;
}

//+------------------------------------------------------------------+
bool TrendBuyAllowed()
{
   if(!UseEMAFilter)
      return true;

   if(CopyBuffer(emaHandle, 0, 0, 1, EMABuffer) <= 0)
      return false;

   double price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   return (price > EMABuffer[0]);
}

//+------------------------------------------------------------------+
bool TrendSellAllowed()
{
   if(!UseEMAFilter)
      return true;

   if(CopyBuffer(emaHandle, 0, 0, 1, EMABuffer) <= 0)
      return false;

   double price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   return (price < EMABuffer[0]);
}

//+------------------------------------------------------------------+
// FIX #6: BEP hanya di-apply sekali per posisi menggunakan flag + ticket
void ManageBreakEven()
{
   ulong ticket = FindPositionTicket();
   if(ticket == 0)
   {
      BEPApplied = false;
      BEPTicket  = 0;
      return;
   }

   // Reset flag kalau posisi berbeda (posisi baru)
   if(ticket != BEPTicket)
   {
      BEPApplied = false;
      BEPTicket  = ticket;
   }

   if(BEPApplied)
      return;

   if(!PositionSelectByTicket(ticket))
      return;

   double openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
   double sl        = PositionGetDouble(POSITION_SL);
   double tp        = PositionGetDouble(POSITION_TP);
   double distance  = MathAbs(openPrice - sl);
   long   type      = PositionGetInteger(POSITION_TYPE);

   if(type == POSITION_TYPE_BUY)
   {
      double price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
      if(price - openPrice >= distance && sl < openPrice)
      {
         if(trade.PositionModify(_Symbol, openPrice, tp))
            BEPApplied = true;
      }
   }

   if(type == POSITION_TYPE_SELL)
   {
      double price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
      if(openPrice - price >= distance && sl > openPrice)
      {
         if(trade.PositionModify(_Symbol, openPrice, tp))
            BEPApplied = true;
      }
   }
}

//+------------------------------------------------------------------+
// FIX #5: HistorySelect hanya dari awal hari ini, bukan dari 0
void CheckLastClosedTrade()
{
   datetime startOfDay = iTime(_Symbol, PERIOD_D1, 0);
   HistorySelect(startOfDay, TimeCurrent());

   int deals = HistoryDealsTotal();
   if(deals == 0) return;

   // Cari deal terakhir yang dari EA ini
   for(int i = deals - 1; i >= 0; i--)
   {
      ulong ticket = HistoryDealGetTicket(i);

      if(ticket == LastProcessedDeal)
         break;

      if(HistoryDealGetInteger(ticket, DEAL_MAGIC) != MagicNumber)
         continue;

      if(HistoryDealGetString(ticket, DEAL_SYMBOL) != _Symbol)
         continue;

      int entry = (int)HistoryDealGetInteger(ticket, DEAL_ENTRY);
      if(entry != DEAL_ENTRY_OUT)
         continue;

      LastProcessedDeal = ticket;

      double profit = HistoryDealGetDouble(ticket, DEAL_PROFIT);

      if(profit < 0)
      {
         ConsecutiveLossCount++;
         SendTelegramMessage("Loss #" + IntegerToString(ConsecutiveLossCount) +
                             "\nSymbol: " + _Symbol +
                             "\nProfit: " + DoubleToString(profit, 2) +" USD"+
                             "\nConsec Loss: " + IntegerToString(ConsecutiveLossCount) + "/" + IntegerToString(MaxConsecutiveLoss));
      }
      else
      {
         ConsecutiveLossCount = 0;
         SendTelegramMessage("Win!\nSymbol: " + _Symbol +
                             "\nProfit: " + DoubleToString(profit, 2)+" USD") ;
      }
      break;
   }
}

//+------------------------------------------------------------------+
bool MaxLossReached()
{
   return (ConsecutiveLossCount >= MaxConsecutiveLoss);
}

//+------------------------------------------------------------------+
// Liquidity Sweep (sudah ada sebelumnya, sekarang diintegrasikan)
bool LiquiditySweepBuy()
{
   if(!UseLiquiditySweep)
      return true;

   double low1  = iLow(_Symbol, SignalTF, 1);
   double low2  = iLow(_Symbol, SignalTF, 2);
   double open  = iOpen(_Symbol, SignalTF, 1);
   double close = iClose(_Symbol, SignalTF, 1);

   bool   sweep      = (low1 < low2);
   double body       = MathAbs(close - open);
   double lowerWick  = MathMin(open, close) - low1;
   bool   rejection  = (lowerWick > body);

   return (sweep && rejection);
}

//+------------------------------------------------------------------+
bool LiquiditySweepSell()
{
   if(!UseLiquiditySweep)
      return true;

   double high1 = iHigh(_Symbol, SignalTF, 1);
   double high2 = iHigh(_Symbol, SignalTF, 2);
   double open  = iOpen(_Symbol, SignalTF, 1);
   double close = iClose(_Symbol, SignalTF, 1);

   bool   sweep     = (high1 > high2);
   double body      = MathAbs(close - open);
   double upperWick = high1 - MathMax(open, close);
   bool   rejection = (upperWick > body);

   return (sweep && rejection);
}

//+------------------------------------------------------------------+
// FIX #11: Tampilkan info EA di chart
void UpdateComment()
{
   double equity    = AccountInfoDouble(ACCOUNT_EQUITY);
   double dailyPnL  = equity - StartEquityToday;
   double pnlPct    = (StartEquityToday > 0) ? (dailyPnL / StartEquityToday * 100.0) : 0;

   string status = "ACTIVE";
   if(MaxLossReached())    status = "STOPPED - Max Loss";
   if(DailyTargetReached()) status = "STOPPED - Target Hit";

   Comment(
      "=== Stochastic RR EA ===",
      "\nStatus    : ", status,
      "\nEquity    : ", DoubleToString(equity, 2),
      "\nDaily P/L : ", DoubleToString(dailyPnL, 2), " (", DoubleToString(pnlPct, 2), "%)",
      "\nConsec Loss: ", ConsecutiveLossCount, "/", MaxConsecutiveLoss,
      "\nBEP Applied: ", BEPApplied ? "Yes" : "No"
   );
}

//+------------------------------------------------------------------+
void OnTick()
{
   ResetDailyEquity();
   CheckLastClosedTrade();
   ManageBreakEven();
   ManageStochasticExit();
   UpdateComment();

   // FIX #10: Notifikasi saat kondisi stop aktif (hanya sekali, pakai flag static)
   static bool notifiedMaxLoss    = false;
   static bool notifiedDailyTarget = false;

   if(MaxLossReached())
   {
      if(!notifiedMaxLoss)
      {
         SendTelegramMessage("🚫 EA berhenti: Max Consecutive Loss tercapai (" +
                             IntegerToString(MaxConsecutiveLoss) + ")\nSymbol: " + _Symbol);
         notifiedMaxLoss = true;
      }
      return;
   }
   else
   {
      notifiedMaxLoss = false;
   }

   if(DailyTargetReached())
   {
      if(!notifiedDailyTarget)
      {
         SendTelegramMessage("🎯 EA berhenti: Daily Target tercapai (" +
                             DoubleToString(DailyTargetPercent, 1) + "%)\nSymbol: " + _Symbol);
         notifiedDailyTarget = true;
      }
      return;
   }
   else
   {
      notifiedDailyTarget = false;
   }

   if(HaveOpenPosition()) return;

   if(CopyBuffer(stochHandle, 0, 0, 3, Kbuffer) <= 0) return;
   if(CopyBuffer(stochHandle, 1, 0, 3, Dbuffer) <= 0) return;

   double K0 = Kbuffer[0], K1 = Kbuffer[1];
   double D0 = Dbuffer[0], D1 = Dbuffer[1];

   bool crossBuy  = (K1 < D1 && K0 > D0);
   bool crossSell = (K1 > D1 && K0 < D0);

   double distance = GetSLdistance();
   if(distance <= 0) return;

   //================ BUY =================
   // FIX #1: hapus double call + FIX #8: tambah LiquiditySweepBuy
   if(K0 < Oversold && crossBuy && TrendBuyAllowed() && LiquiditySweepBuy())
   {
      double price = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
      double sl    = NormalizeDouble(price - distance, _Digits);
      double tp    = NormalizeDouble(price + distance * RR, _Digits);

      if(trade.Buy(LotSize, _Symbol, price, sl, tp))
      {
         BEPApplied = false;
         BEPTicket  = 0;

         string msg = "🟢 BUY " + _Symbol +
                      "\nTimeframe: " + EnumToString(SignalTF) +
                      "\nEntry Price: "     + DoubleToString(price, _Digits) +
                      "\nSL: "        + DoubleToString(sl, _Digits) +
                      "\nTP: "        + DoubleToString(tp, _Digits) +
                      "\nLot: "       + DoubleToString(LotSize, 2);
         SendTelegramMessage(msg);
      }
   }

   //================ SELL =================
   // FIX #1: hapus double call + FIX #8: tambah LiquiditySweepSell
   if(K0 > Overbought && crossSell && TrendSellAllowed() && LiquiditySweepSell())
   {
      double price = SymbolInfoDouble(_Symbol, SYMBOL_BID);
      double sl    = NormalizeDouble(price + distance, _Digits);
      double tp    = NormalizeDouble(price - distance * RR, _Digits);

      if(trade.Sell(LotSize, _Symbol, price, sl, tp))
      {
         BEPApplied = false;
         BEPTicket  = 0;

         string msg = "🔴 SELL " + _Symbol +
                      "\nTimeframe: " + EnumToString(SignalTF) +
                      "\nEntry Price: "     + DoubleToString(price, _Digits) +
                      "\nSL: "        + DoubleToString(sl, _Digits) +
                      "\nTP: "        + DoubleToString(tp, _Digits) +
                      "\nLot: "       + DoubleToString(LotSize, 2);
         SendTelegramMessage(msg);
      }
   }
}

//+------------------------------------------------------------------+
string UrlEncode(string text)
{
   string encoded = "";
   uchar  c;

   for(int i = 0; i < StringLen(text); i++)
   {
      c = (uchar)StringGetCharacter(text, i);

      if((c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == '-' || c == '_' || c == '.' || c == '~')
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

//+------------------------------------------------------------------+
void SendTelegramMessage(string text)
{
   if(StringLen(TelegramBotToken) == 0 || StringLen(TelegramChatID) == 0)
      return;

   string url  = "https://api.telegram.org/bot" + TelegramBotToken + "/sendMessage";
   string data = "chat_id=" + TelegramChatID + "&text=" + UrlEncode(text);

   char   result[];
   char   post[];
   string headers = "Content-Type: application/x-www-form-urlencoded\r\n";

   StringToCharArray(data, post, 0, StringLen(data));

   int res = WebRequest("POST", url, headers, 5000, post, result, headers);

   if(res == -1)
      Print("Telegram Error: ", GetLastError());
   else
      Print("Telegram sent: ", text);
}
//+------------------------------------------------------------------+