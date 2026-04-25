/*
 * Libation Locker by Dan Roberts
 *
 * Local, self-hosted ESP32 web app for tracking spirits / bottle inventory.
 * - AP-first with optional STA join
 * - LittleFS persistence
 * - JSON REST API + single-page UI
 *
 * Creator: Dan Roberts
 * License: GPL-3.0
 */

#include <Arduino.h>
#include <WiFi.h>

#include "AppConfig.h"
#include "AppSerial.h"
#include "InventoryStore.h"
#include "WebServerLL.h"
#include "AppWiFi.h"
static void printNetworkInfo() {
  APP_SERIAL.println("[NET] Mode: AP always-on + optional STA");
  APP_SERIAL.print("[NET] AP SSID: "); APP_SERIAL.println(AppWiFi::apSsid());
  APP_SERIAL.print("[NET] AP IP:   "); APP_SERIAL.println(AppWiFi::apIP());
  // Credentials (printed to serial only)
  const auto& cfg = AppConfig::get();
  APP_SERIAL.print("[NET] AP PASS: "); APP_SERIAL.println(cfg.apPass);
  if (cfg.webUser.length()) {
    APP_SERIAL.print("[WEB] Basic Auth user: "); APP_SERIAL.println(cfg.webUser);
    APP_SERIAL.print("[WEB] Basic Auth pass: "); APP_SERIAL.println(cfg.webPass);
  } else {
    APP_SERIAL.println("[WEB] Basic Auth disabled");
  }
  if (AppWiFi::staConnected()) {
    APP_SERIAL.print("[NET] STA SSID: "); APP_SERIAL.println(AppWiFi::staSsid());
    APP_SERIAL.print("[NET] STA IP:   "); APP_SERIAL.println(AppWiFi::staIP());
  } else {
    APP_SERIAL.println("[NET] STA not connected");
  }
}


void setup() {
  APP_SERIAL.begin(115200);
  delay(200);

  APP_SERIAL.println();
  APP_SERIAL.printf("[BOOT] Heap total=%u free=%u maxAlloc=%u\n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  if (psramFound()) {
    APP_SERIAL.printf("[BOOT] PSRAM total=%u free=%u\n", ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    APP_SERIAL.println("[BOOT] PSRAM not found");
  }
  APP_SERIAL.println("=== LibationLocker by Dan ===");
  AppConfig::begin();
  InventoryStore::begin();
  // WiFi (AP always on; STA configurable in web UI)
  AppWiFi::begin();

  WebServerLL::begin();

  printNetworkInfo();
  APP_SERIAL.println("[BOOT] Ready");
}

// Heap health thresholds
static constexpr uint32_t HEAP_WARN_THRESHOLD  = 30000;  // 30 KB — warning
static constexpr uint32_t HEAP_CRIT_THRESHOLD  = 15000;  // 15 KB — critical
static uint32_t s_lastHeapLogMs = 0;
static bool     s_heapWarned = false;

void loop() {
  AppWiFi::loop();

  // Periodic heap health check (every 30 seconds, or immediately if critical)
  uint32_t now = millis();
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxAlloc = ESP.getMaxAllocHeap();

  if (freeHeap < HEAP_CRIT_THRESHOLD) {
    // Log immediately on critical — don't wait for interval
    if (!s_heapWarned || (now - s_lastHeapLogMs >= 5000)) {
      APP_SERIAL.printf("[HEAP] CRITICAL free=%u maxAlloc=%u minEver=%u items=%u\n",
                        freeHeap, maxAlloc, ESP.getMinFreeHeap(),
                        InventoryStore::count());
      s_lastHeapLogMs = now;
      s_heapWarned = true;
    }
  } else if (now - s_lastHeapLogMs >= 30000) {
    if (freeHeap < HEAP_WARN_THRESHOLD) {
      APP_SERIAL.printf("[HEAP] WARNING free=%u maxAlloc=%u minEver=%u\n",
                        freeHeap, maxAlloc, ESP.getMinFreeHeap());
    }
    // Normal status every 30s (only if at warning level or first time)
    s_lastHeapLogMs = now;
    s_heapWarned = false;
  }

  delay(10);
}
