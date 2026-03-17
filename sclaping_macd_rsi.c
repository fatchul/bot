//+------------------------------------------------------------------+
//|                                               DayTrade-Scalp.mq5  |
//|                                                FIXED VERSION      |
//|                                         DENGAN VALIDASI SL/TP     |
//+------------------------------------------------------------------+
// #property copyright “J2themoons”
#property version   "7.0"
#property description "FIXED: Stop Loss validation untuk XAUUSD"
#property description "PASTI BISA ENTRY - SL/TP sesuai aturan broker"

#include <Trade/Trade.mqh>

//--- INPUT PARAMETERS - AMAN UNTUK XAUUSD
input group "=== TIMEFRAME ==="
input ENUM_TIMEFRAMES InpTimeframe     = PERIOD_M5;     // M5

input group "=== RSI SETTINGS ==="
input int            InpRSIPeriod      = 10;             // RSI Period
input int            InpRSIOversold    = 12;            // Oversold
input int            InpRSIOverbought  = 70;            // Overbought

input group "=== MACD SETTINGS ==="
input int            InpMACDFast       = 8;             // Fast
input int            InpMACDSlow       = 17;            // Slow
input int            InpMACDSignal     = 7;             // Signal

input group "=== MONEY MANAGEMENT - AMAN ==="
input double         InpFixedLot       = 0.04;          // Lot kecil
input int            InpStopLossPips   = 600;            // SL BESAR (50 pips)
input int            InpTakeProfitPips = 300;            // TP kecil (30 pips)
input int            InpMagicNumber    = 123457;

input group "=== FILTER ==="
input int            InpMaxSpreadPips  = 60;            // Max spread

//--- global variables
int            rsiHandle;
int            macdHandle;
CTrade         trade;
datetime       lastBarTime;
double         tickSize;
double         pointValue;
int            minStopDistance;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
   trade.SetExpertMagicNumber(InpMagicNumber);
   trade.SetDeviationInPoints(50);
   trade.SetTypeFillingBySymbol(_Symbol);
   
   tickSize = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);
   pointValue = _Point;
   
   // Dapatkan minimal jarak stop loss dari broker
   minStopDistance = (int)SymbolInfoInteger(_Symbol, SYMBOL_TRADE_STOPS_LEVEL);
   if(minStopDistance == 0) minStopDistance = 200; // Default 20 pips (200 points)
   
   Print("========== EA FIXED ==========");
   Print("Minimal Stop Distance: ", minStopDistance/10, " pips");
   Print("Stop Loss Setting: ", InpStopLossPips, " pips");
   Print("Take Profit Setting: ", InpTakeProfitPips, " pips");
   
   // Validasi input
   if(InpStopLossPips * 10 < minStopDistance)
   {
      Print("⚠️ PERINGATAN: Stop Loss terlalu kecil!");
      Print("Minimal ", minStopDistance/10, " pips, tapi Anda setting ", InpStopLossPips, " pips");
      Print("EA akan menggunakan minimal ", minStopDistance/10, " pips");
   }
   
   // Create indicators
   rsiHandle = iRSI(_Symbol, InpTimeframe, InpRSIPeriod, PRICE_CLOSE);
   macdHandle = iMACD(_Symbol, InpTimeframe, InpMACDFast, InpMACDSlow, InpMACDSignal, PRICE_CLOSE);
   
   if(rsiHandle == INVALID_HANDLE || macdHandle == INVALID_HANDLE)
   {
      Print("Failed to create indicator handles");
      return INIT_FAILED;
   }
   
   lastBarTime = iTime(_Symbol, InpTimeframe, 0);
   
   return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Get entry signal - SEDERHANA                                    |
//+------------------------------------------------------------------+
int GetEntrySignal()
{
   double rsi[];
   double macdMain[];
   double macdSignal[];
   
   ArraySetAsSeries(rsi, true);
   ArraySetAsSeries(macdMain, true);
   ArraySetAsSeries(macdSignal, true);
   
   int rsiCopied = CopyBuffer(rsiHandle, 0, 1, 2, rsi);
   int macdMainCopied = CopyBuffer(macdHandle, 0, 1, 2, macdMain);
   int macdSignalCopied = CopyBuffer(macdHandle, 1, 1, 2, macdSignal);
   
   if(rsiCopied < 2 || macdMainCopied < 2 || macdSignalCopied < 2) 
      return 0;
   
   // BUY: RSI oversold
   if(rsi[1] < InpRSIOversold && rsi[0] > InpRSIOversold)
   {
      return 1;
   }
   
   // SELL: RSI overbought
   if(rsi[1] > InpRSIOverbought && rsi[0] < InpRSIOverbought)
   {
      return -1;
   }
   
   return 0;
}

//+------------------------------------------------------------------+
//| Validasi Stop Loss - FUNGSI BARU                                |
//+------------------------------------------------------------------+
bool ValidateStopLoss(double entryPrice, double &sl, double &tp, int type)
{
   // Hitung jarak SL dalam points
   double slDistance;
   if(type == POSITION_TYPE_BUY)
      slDistance = (entryPrice - sl) / pointValue;
   else
      slDistance = (sl - entryPrice) / pointValue;
   
   // Cek spread
   long spreadLong = SymbolInfoInteger(_Symbol, SYMBOL_SPREAD);
   double spread = (double)spreadLong;
   double spreadPips = spread / 10.0;
   
   if(spreadPips > InpMaxSpreadPips)
   {
      Print("❌ Spread terlalu tinggi: ", spreadPips, " pips");
      return false;
   }
   
   // Validasi jarak SL minimal
   if(slDistance < minStopDistance)
   {
      Print("⚠️ SL terlalu dekat: ", slDistance/10, " pips (minimal ", minStopDistance/10, " pips)");
      
      // Sesuaikan SL ke jarak minimal
      if(type == POSITION_TYPE_BUY)
         sl = entryPrice - minStopDistance * pointValue;
      else
         sl = entryPrice + minStopDistance * pointValue;
      
      Print("✅ SL disesuaikan ke ", slDistance/10, " pips");
   }
   
   // Normalisasi harga
   sl = NormalizeDouble(MathRound(sl / tickSize) * tickSize, _Digits);
   tp = NormalizeDouble(MathRound(tp / tickSize) * tickSize, _Digits);
   
   return true;
}

//+------------------------------------------------------------------+
//| Check margin                                                    |
//+------------------------------------------------------------------+
bool CheckMargin()
{
   double freeMargin = AccountInfoDouble(ACCOUNT_MARGIN_FREE);
   double requiredMargin = 50.0; // Estimasi untuk 0.01 lot XAUUSD
   
   if(freeMargin < requiredMargin)
   {
      Print("❌ Margin tidak cukup: $", freeMargin, " < $", requiredMargin);
      return false;
   }
   return true;
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
   // Cek setiap bar baru
   datetime currentBarTime = iTime(_Symbol, InpTimeframe, 0);
   if(currentBarTime == lastBarTime)
      return;
   lastBarTime = currentBarTime;
   
   // Jika sudah ada posisi, exit sederhana
   if(PositionSelect(_Symbol))
   {
      // Hold maksimal 5 menit
      static datetime entryTime = 0;
      if(entryTime == 0) entryTime = TimeCurrent();
      
      if(TimeCurrent() - entryTime > 300) // 5 menit
      {
         trade.PositionClose(_Symbol);
         Print("🔚 Position closed after 5 minutes");
         entryTime = 0;
      }
      return;
   }
   
   // Reset entry time
   static datetime entryTime = 0;
   entryTime = 0;
   
   // Cek margin
   if(!CheckMargin()) return;
   
   // Get entry signal
   int signal = GetEntrySignal();
   
   if(signal == 0) return;
   
   // Ambil harga
   double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   double spread = (ask - bid) / pointValue / 10;
   
   Print("=================================");
   Print("🔍 SIGNAL DETECTED at ", TimeToString(TimeCurrent()));
   Print("Signal: ", signal == 1 ? "BUY" : "SELL");
   Print("Spread: ", spread, " pips");
   
   if(signal == 1) // BUY
   {
      // Hitung SL/TP dengan aman
      double sl = ask - InpStopLossPips * 10 * pointValue;
      double tp = ask + InpTakeProfitPips * 10 * pointValue;
      
      // Validasi SL/TP
      if(!ValidateStopLoss(ask, sl, tp, POSITION_TYPE_BUY))
      {
         Print("❌ Validasi gagal, skip entry");
         return;
      }
      
      Print("📈 EXECUTING BUY");
      Print("Entry: ", ask);
      Print("SL: ", sl, " (", (ask-sl)/pointValue/10, " pips)");
      Print("TP: ", tp, " (", (tp-ask)/pointValue/10, " pips)");
      
      if(trade.Buy(InpFixedLot, _Symbol, ask, sl, tp, "Fixed Buy"))
      {
         Print("✅ ORDER SUCCESS");
         entryTime = TimeCurrent();
      }
      else
      {
         Print("❌ ORDER FAILED: ", trade.ResultRetcodeDescription());
      }
   }
   else if(signal == -1) // SELL
   {
      // Hitung SL/TP dengan aman
      double sl = bid + InpStopLossPips * 10 * pointValue;
      double tp = bid - InpTakeProfitPips * 10 * pointValue;
      
      // Validasi SL/TP
      if(!ValidateStopLoss(bid, sl, tp, POSITION_TYPE_SELL))
      {
         Print("❌ Validasi gagal, skip entry");
         return;
      }
      
      Print("📉 EXECUTING SELL");
      Print("Entry: ", bid);
      Print("SL: ", sl, " (", (sl-bid)/pointValue/10, " pips)");
      Print("TP: ", tp, " (", (bid-tp)/pointValue/10, " pips)");
      
      if(trade.Sell(InpFixedLot, _Symbol, bid, sl, tp, "Fixed Sell"))
      {
         Print("✅ ORDER SUCCESS");
         entryTime = TimeCurrent();
      }
      else
      {
         Print("❌ ORDER FAILED: ", trade.ResultRetcodeDescription());
      }
   }
   Print("=================================");
}

//+------------------------------------------------------------------+
//| Expert deinitialization                                          |
//+------------------------------------------------------------------+
void OnDeinit(const int reason)
{
   if(rsiHandle != INVALID_HANDLE) IndicatorRelease(rsiHandle);
   if(macdHandle != INVALID_HANDLE) IndicatorRelease(macdHandle);
   
   Print("========== EA STOPPED ==========");
}
//+------------------------------------------------------------------+