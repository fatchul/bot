//+------------------------------------------------------------------+
//|                                               DayTrade-Scalp.mq5  |
//|                                                FIXED VERSION      |
//|                                         DENGAN VALIDASI SL/TP     |
//+------------------------------------------------------------------+
#property copyright "J2themoons"
#property version   "7.4"
#property description "FIXED: Stop Loss validation untuk XAUUSD"
#property description "DENGAN PROTEKSI 3x SL PER HARI"
#property description "DENGAN PROTEKSI MODAL & DRAWDOWN"
#property description "DENGAN TARGET HARIAN"
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
input int            InpStopLossPips   = 600;           // SL BESAR (50 pips)
input int            InpTakeProfitPips = 300;           // TP kecil (30 pips)
input int            InpMagicNumber    = 123457;

input group "=== FILTER ==="
input int            InpMaxSpreadPips  = 60;            // Max spread

input group "=== PROTEKSI SL HARIAN ==="
input int            InpMaxSLPerDay    = 3;             // Maksimal SL per hari
input bool           InpResetAtMidnight = true;         // Reset hitungan SL setiap tengah malam

input group "=== PROTEKSI MODAL & DRAWDOWN ==="
input double         InpMaxEquityLossPercent = 10.0;    // Max equity loss (%)
input double         InpMaxDrawdownPercent = 10.0;      // Max drawdown sebelum cut all (%)
input bool           InpEnableCutLoss = true;           // Aktifkan cut loss manual
input double         InpCutLossPercent = 10.0;          // Cut loss di persentase tertentu

input group "=== TARGET HARIAN ==="
input bool           InpEnableDailyTarget = true;       // Aktifkan target harian
input double         InpDailyTargetPercent = 5.0;       // Target harian (% dari modal)
input bool           InpStopAfterTarget = true;         // Berhenti trading setelah target tercapai
input bool           InpResetTargetDaily = true;        // Reset target setiap hari

//--- global variables
int            rsiHandle;
int            macdHandle;
CTrade         trade;
datetime       lastBarTime;
double         tickSize;
double         pointValue;
int            minStopDistance;

//--- Proteksi SL Harian
int            stopLossCount = 0;           // Hitungan SL hari ini
datetime       lastSLDate = 0;               // Tanggal terakhir SL terjadi
datetime       currentTradingDay = 0;        // Tanggal trading saat ini
bool           tradingSuspended = false;     // Status trading suspend
datetime       lastSuspendPrintTime = 0;     // Waktu terakhir print status suspend

//--- Proteksi Modal & Drawdown
double         initialEquity = 0;             // Equity awal saat EA start
double         highestEquity = 0;              // Equity tertinggi
double         lowestEquity = 0;               // Equity terendah
bool           equityStopTrading = false;      // Stop trading karena equity loss
bool           drawdownStopTrading = false;    // Stop trading karena drawdown
bool           cutLossExecuted = false;        // Sudah melakukan cut loss
datetime       lastEquityCheck = 0;            // Waktu terakhir cek equity

//--- Target Harian
double         dayInitialEquity = 0;           // Equity awal hari ini
double         dayHighestEquity = 0;            // Equity tertinggi hari ini
double         dayProfit = 0;                    // Profit hari ini
double         dayProfitPercent = 0;              // Persentase profit hari ini
bool           dailyTargetReached = false;      // Target harian tercapai
datetime       targetReachedTime = 0;           // Waktu target tercapai
bool           targetStopTrading = false;       // Stop trading karena target tercapai

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
   
   // Inisialisasi equity
   initialEquity = AccountInfoDouble(ACCOUNT_EQUITY);
   highestEquity = initialEquity;
   lowestEquity = initialEquity;
   
   // Inisialisasi target harian
   dayInitialEquity = initialEquity;
   dayHighestEquity = initialEquity;
   dayProfit = 0;
   dayProfitPercent = 0;
   
   Print("========== EA FIXED ==========");
   Print("Minimal Stop Distance: ", minStopDistance/10, " pips");
   Print("Stop Loss Setting: ", InpStopLossPips, " pips");
   Print("Take Profit Setting: ", InpTakeProfitPips, " pips");
   Print("Proteksi: Max ", InpMaxSLPerDay, " SL per hari");
   Print("=== PROTEKSI MODAL ===");
   Print("Initial Equity: $", DoubleToString(initialEquity, 2));
   Print("Max Equity Loss: ", InpMaxEquityLossPercent, "%");
   Print("Max Drawdown: ", InpMaxDrawdownPercent, "%");
   Print("Cut Loss: ", InpCutLossPercent, "% (", InpEnableCutLoss ? "Aktif" : "Nonaktif", ")");
   Print("=== TARGET HARIAN ===");
   Print("Daily Target: ", InpDailyTargetPercent, "% (", InpEnableDailyTarget ? "Aktif" : "Nonaktif", ")");
   Print("Stop After Target: ", InpStopAfterTarget ? "Ya" : "Tidak");
   
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
   currentTradingDay = GetTradingDay();
   lastSuspendPrintTime = 0;
   lastEquityCheck = 0;
   targetReachedTime = 0;
   
   return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
//| Mendapatkan tanggal trading saat ini                            |
//+------------------------------------------------------------------+
datetime GetTradingDay()
{
   MqlDateTime dt;
   TimeCurrent(dt);
   dt.hour = 0;
   dt.min = 0;
   dt.sec = 0;
   return StructToTime(dt);
}

//+------------------------------------------------------------------+
//| Reset hitungan SL dan target jika berganti hari                 |
//+------------------------------------------------------------------+
void CheckAndResetDaily()
{
   if(!InpResetAtMidnight && !InpResetTargetDaily) return;
   
   datetime today = GetTradingDay();
   
   // Jika sudah berganti hari
   if(today != currentTradingDay)
   {
      // Reset SL count
      if(InpResetAtMidnight)
      {
         if(stopLossCount >= InpMaxSLPerDay)
         {
            Print("📅 GANTI HARI - Reset hitungan SL dari ", stopLossCount, " menjadi 0");
         }
         stopLossCount = 0;
         tradingSuspended = false;
      }
      
      // Reset target harian
      if(InpResetTargetDaily)
      {
         double currentEquity = AccountInfoDouble(ACCOUNT_EQUITY);
         
         Print("📅 GANTI HARI - Reset target harian");
         Print("   Kemarin: Profit $", DoubleToString(dayProfit, 2), " (", DoubleToString(dayProfitPercent, 2), "%)");
         Print("   Equity baru: $", DoubleToString(currentEquity, 2));
         
         dayInitialEquity = currentEquity;
         dayHighestEquity = currentEquity;
         dayProfit = 0;
         dayProfitPercent = 0;
         dailyTargetReached = false;
         targetStopTrading = false;
         targetReachedTime = 0;
      }
      
      currentTradingDay = today;
      lastSuspendPrintTime = 0; // Reset timer print
      Print("📅 Hari baru dimulai - ", TimeToString(today));
   }
}

//+------------------------------------------------------------------+
//| Monitor target harian                                           |
//+------------------------------------------------------------------+
void MonitorDailyTarget()
{
   if(!InpEnableDailyTarget) return;
   if(dailyTargetReached) return;
   
   double currentEquity = AccountInfoDouble(ACCOUNT_EQUITY);
   
   // Update highest equity hari ini
   if(currentEquity > dayHighestEquity)
      dayHighestEquity = currentEquity;
   
   // Hitung profit hari ini
   dayProfit = currentEquity - dayInitialEquity;
   dayProfitPercent = (dayProfit / dayInitialEquity) * 100;
   
   // Cek apakah target tercapai
   if(dayProfitPercent >= InpDailyTargetPercent)
   {
      dailyTargetReached = true;
      targetReachedTime = TimeCurrent();
      
      Print("🎯 TARGET HARIAN TERCAPAI!");
      Print("   Target: ", InpDailyTargetPercent, "%");
      Print("   Profit: $", DoubleToString(dayProfit, 2), " (", DoubleToString(dayProfitPercent, 2), "%)");
      Print("   Equity: $", DoubleToString(currentEquity, 2));
      Print("   Waktu: ", TimeToString(targetReachedTime));
      
      if(InpStopAfterTarget)
      {
         targetStopTrading = true;
         Print("🚫 TRADING DIHENTIKAN - Target harian tercapai");
         
         // Tutup semua posisi jika ada
         if(PositionsTotal() > 0)
         {
            Print("🔪 Menutup semua posisi...");
            CloseAllPositions();
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Monitor equity dan drawdown                                     |
//+------------------------------------------------------------------+
void MonitorEquityAndDrawdown()
{
   double currentEquity = AccountInfoDouble(ACCOUNT_EQUITY);
   double currentBalance = AccountInfoDouble(ACCOUNT_BALANCE);
   double currentProfit = currentEquity - initialEquity;
   double equityLossPercent = (currentProfit / initialEquity) * 100;
   
   // Update highest dan lowest equity
   if(currentEquity > highestEquity)
      highestEquity = currentEquity;
   if(currentEquity < lowestEquity)
      lowestEquity = currentEquity;
   
   // Hitung drawdown dari peak
   double drawdownFromPeak = 0;
   if(highestEquity > 0)
      drawdownFromPeak = ((highestEquity - currentEquity) / highestEquity) * 100;
   
   // Cek equity loss
   if(equityLossPercent <= -InpMaxEquityLossPercent && !equityStopTrading)
   {
      equityStopTrading = true;
      Print("🚫 EMERGENCY STOP - Equity loss ", DoubleToString(equityLossPercent, 2), "%");
      Print("🚫 Melebihi batas ", InpMaxEquityLossPercent, "%");
      Print("Current Equity: $", DoubleToString(currentEquity, 2));
      Print("Initial Equity: $", DoubleToString(initialEquity, 2));
      
      // Tutup semua posisi
      CloseAllPositions();
   }
   
   // Cek drawdown
   if(drawdownFromPeak >= InpMaxDrawdownPercent && !drawdownStopTrading)
   {
      drawdownStopTrading = true;
      Print("🚫 DRAWDOWN ALERT - Drawdown ", DoubleToString(drawdownFromPeak, 2), "%");
      Print("🚫 Melebihi batas ", InpMaxDrawdownPercent, "%");
      Print("Highest Equity: $", DoubleToString(highestEquity, 2));
      Print("Current Equity: $", DoubleToString(currentEquity, 2));
      
      // Tutup semua posisi
      CloseAllPositions();
   }
   
   // Cek apakah perlu cut loss manual
   if(InpEnableCutLoss && !cutLossExecuted)
   {
      double lossFromPeak = ((highestEquity - currentEquity) / highestEquity) * 100;
      if(lossFromPeak >= InpCutLossPercent)
      {
         Print("🔪 CUT LOSS MANUAL - Loss ", DoubleToString(lossFromPeak, 2), "% dari peak");
         Print("🔪 Menutup semua posisi...");
         
         // Tutup semua posisi
         CloseAllPositions();
         
         cutLossExecuted = true;
         drawdownStopTrading = true; // Stop trading setelah cut loss
      }
   }
   
   // Print status equity secara periodik (setiap 1 jam)
   datetime currentTime = TimeCurrent();
   if(currentTime - lastEquityCheck >= 3600)
   {
      Print("=== STATUS EQUITY ===");
      Print("Current Equity: $", DoubleToString(currentEquity, 2));
      Print("Balance: $", DoubleToString(currentBalance, 2));
      Print("Total Profit/Loss: $", DoubleToString(currentProfit, 2), " (", DoubleToString(equityLossPercent, 2), "%)");
      Print("Highest Equity: $", DoubleToString(highestEquity, 2));
      Print("Drawdown from peak: ", DoubleToString(drawdownFromPeak, 2), "%");
      
      if(InpEnableDailyTarget)
      {
         Print("=== TARGET HARIAN ===");
         Print("Hari ini: $", DoubleToString(dayProfit, 2), " (", DoubleToString(dayProfitPercent, 2), "%)");
         Print("Target: ", InpDailyTargetPercent, "%");
         Print("Status: ", dailyTargetReached ? "✅ Tercapai" : "⏳ Progress");
      }
      Print("====================");
      lastEquityCheck = currentTime;
   }
}

//+------------------------------------------------------------------+
//| Tutup semua posisi                                              |
//+------------------------------------------------------------------+
void CloseAllPositions()
{
   int closed = 0;
   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong ticket = PositionGetTicket(i);
      if(PositionSelectByTicket(ticket))
      {
         if(PositionGetString(POSITION_SYMBOL) == _Symbol && 
            PositionGetInteger(POSITION_MAGIC) == InpMagicNumber)
         {
            if(trade.PositionClose(ticket))
            {
               Print("🔒 Menutup posisi #", ticket, " berhasil");
               closed++;
            }
            else
            {
               Print("❌ Gagal menutup posisi #", ticket);
            }
         }
      }
   }
   if(closed > 0)
      Print("✅ Total ", closed, " posisi ditutup");
}

//+------------------------------------------------------------------+
//| Cek apakah trading diizinkan                                    |
//+------------------------------------------------------------------+
bool IsTradingAllowed()
{
   CheckAndResetDaily();
   
   // Cek target harian
   if(targetStopTrading)
   {
      Print("🚫 TRADING SUSPENDED - Target harian ", InpDailyTargetPercent, "% tercapai");
      return false;
   }
   
   // Cek equity stop
   if(equityStopTrading)
   {
      Print("🚫 TRADING SUSPENDED - Equity loss melebihi batas");
      return false;
   }
   
   // Cek drawdown stop
   if(drawdownStopTrading)
   {
      Print("🚫 TRADING SUSPENDED - Drawdown melebihi batas");
      return false;
   }
   
   // Cek SL harian
   if(tradingSuspended)
   {
      return false;
   }
   
   if(stopLossCount >= InpMaxSLPerDay)
   {
      tradingSuspended = true;
      Print("🚫 TRADING DIHENTIKAN - Mencapai batas maksimal ", InpMaxSLPerDay, " SL per hari");
      return false;
   }
   
   return true;
}

//+------------------------------------------------------------------+
//| Catat stop loss                                                 |
//+------------------------------------------------------------------+
void RecordStopLoss()
{
   stopLossCount++;
   lastSLDate = TimeCurrent();
   
   Print("⚠️ STOP LOSS #", stopLossCount, " terjadi pada ", TimeToString(lastSLDate));
   
   if(stopLossCount >= InpMaxSLPerDay)
   {
      tradingSuspended = true;
      Print("🚫 PERHATIAN: Sudah mencapai ", stopLossCount, " SL hari ini!");
      Print("🚫 Trading akan dihentikan hingga hari berikutnya");
   }
}

//+------------------------------------------------------------------+
//| Catat take profit                                               |
//+------------------------------------------------------------------+
void RecordTakeProfit()
{
   // Update target harian setelah TP
   if(InpEnableDailyTarget && !dailyTargetReached)
   {
      double currentEquity = AccountInfoDouble(ACCOUNT_EQUITY);
      dayProfit = currentEquity - dayInitialEquity;
      dayProfitPercent = (dayProfit / dayInitialEquity) * 100;
      
      Print("💰 TAKE PROFIT - Profit hari ini: ", DoubleToString(dayProfitPercent, 2), "%");
   }
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
      
      Print("✅ SL disesuaikan ke ", minStopDistance/10, " pips");
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
//| Monitor posisi untuk deteksi SL/TP                              |
//+------------------------------------------------------------------+
void MonitorPositions()
{
   // Cek apakah ada posisi yang kena SL atau TP
   if(PositionSelect(_Symbol))
   {
      // Dapatkan harga entry, SL, dan TP
      double entryPrice = PositionGetDouble(POSITION_PRICE_OPEN);
      double slPrice = PositionGetDouble(POSITION_SL);
      double tpPrice = PositionGetDouble(POSITION_TP);
      double currentPrice;
      
      int positionType = (int)PositionGetInteger(POSITION_TYPE);
      
      if(positionType == POSITION_TYPE_BUY)
         currentPrice = SymbolInfoDouble(_Symbol, SYMBOL_BID);
      else
         currentPrice = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
      
      // Cek apakah posisi kena SL
      bool isStopped = false;
      bool isTaken = false;
      
      if(positionType == POSITION_TYPE_BUY)
      {
         if(currentPrice <= slPrice && slPrice > 0)
            isStopped = true;
         if(currentPrice >= tpPrice && tpPrice > 0)
            isTaken = true;
      }
      else // SELL
      {
         if(currentPrice >= slPrice && slPrice > 0)
            isStopped = true;
         if(currentPrice <= tpPrice && tpPrice > 0)
            isTaken = true;
      }
      
      // Jika kena SL, catat
      if(isStopped)
      {
         // Tunggu sampai posisi benar-benar closed
         Sleep(1000);
         if(!PositionSelect(_Symbol))
         {
            RecordStopLoss();
         }
      }
      
      // Jika kena TP, catat
      if(isTaken)
      {
         // Tunggu sampai posisi benar-benar closed
         Sleep(1000);
         if(!PositionSelect(_Symbol))
         {
            RecordTakeProfit();
         }
      }
   }
}

//+------------------------------------------------------------------+
//| Tampilkan status suspend secara periodik                        |
//+------------------------------------------------------------------+
void PrintSuspendStatus()
{
   if(!tradingSuspended && !equityStopTrading && !drawdownStopTrading && !targetStopTrading) return;
   
   datetime currentTime = TimeCurrent();
   // Print setiap 3600 detik (1 jam)
   if(currentTime - lastSuspendPrintTime >= 3600)
   {
      string reason = "";
      if(targetStopTrading)
         reason = "Target Harian Tercapai";
      else if(equityStopTrading)
         reason = "Equity Loss";
      else if(drawdownStopTrading)
         reason = "Drawdown";
      else if(tradingSuspended)
         reason = "Max SL Harian";
      
      Print("⏸️ Trading suspended - [", reason, "] Menunggu reset...");
      
      if(tradingSuspended)
         Print("   SL hari ini: ", stopLossCount, "/", InpMaxSLPerDay);
      if(targetStopTrading)
         Print("   Target: ", InpDailyTargetPercent, "% tercapai pukul ", TimeToString(targetReachedTime));
      
      lastSuspendPrintTime = currentTime;
   }
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
   // Monitor equity dan drawdown
   MonitorEquityAndDrawdown();
   
   // Monitor target harian
   MonitorDailyTarget();
   
   // Monitor posisi untuk deteksi SL/TP
   MonitorPositions();
   
   // Cek setiap bar baru
   datetime currentBarTime = iTime(_Symbol, InpTimeframe, 0);
   if(currentBarTime == lastBarTime)
      return;
   lastBarTime = currentBarTime;
   
   // Cek apakah trading diizinkan
   if(!IsTradingAllowed())
   {
      // Tampilkan status suspend secara periodik
      PrintSuspendStatus();
      return;
   }
   
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
   Print("SL Count hari ini: ", stopLossCount, "/", InpMaxSLPerDay);
   
   double currentEquity = AccountInfoDouble(ACCOUNT_EQUITY);
   double equityLossPercent = ((currentEquity - initialEquity) / initialEquity) * 100;
   Print("Total Equity: $", DoubleToString(currentEquity, 2), " (", DoubleToString(equityLossPercent, 2), "%)");
   
   if(InpEnableDailyTarget)
   {
      Print("Hari ini: $", DoubleToString(dayProfit, 2), " (", DoubleToString(dayProfitPercent, 2), "%) / ", InpDailyTargetPercent, "%");
   }
   
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
   Print("Statistik SL hari terakhir: ", stopLossCount);
   
   double finalEquity = AccountInfoDouble(ACCOUNT_EQUITY);
   double totalProfit = finalEquity - initialEquity;
   double totalProfitPercent = (totalProfit / initialEquity) * 100;
   
   Print("=== SUMMARY TOTAL ===");
   Print("Initial Equity: $", DoubleToString(initialEquity, 2));
   Print("Final Equity: $", DoubleToString(finalEquity, 2));
   Print("Total Profit/Loss: $", DoubleToString(totalProfit, 2), " (", DoubleToString(totalProfitPercent, 2), "%)");
   Print("Highest Equity: $", DoubleToString(highestEquity, 2));
   Print("Max Drawdown: ", DoubleToString(((highestEquity - lowestEquity) / highestEquity) * 100, 2), "%");
   
   if(InpEnableDailyTarget && dayInitialEquity > 0)
   {
      Print("=== SUMMARY HARI INI ===");
      Print("Start Equity: $", DoubleToString(dayInitialEquity, 2));
      Print("Profit Today: $", DoubleToString(dayProfit, 2), " (", DoubleToString(dayProfitPercent, 2), "%)");
      Print("Target: ", InpDailyTargetPercent, "%");
      Print("Status: ", dailyTargetReached ? "✅ Tercapai" : "❌ Belum Tercapai");
      if(targetReachedTime > 0)
         Print("Waktu Tercapai: ", TimeToString(targetReachedTime));
   }
}
//+------------------------------------------------------------------+