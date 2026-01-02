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
  APP_SERIAL.println("=== LibationLocker by Dan ===");
  AppConfig::begin();
  InventoryStore::begin();
  // WiFi (AP always on; STA configurable in web UI)
  AppWiFi::begin();

  WebServerLL::begin();

printNetworkInfo();
  APP_SERIAL.println("[BOOT] Ready");
}

void loop() {
  AppWiFi::loop();
  delay(10);
}
