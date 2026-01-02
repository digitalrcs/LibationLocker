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

#pragma once
#include <Arduino.h>

struct RuntimeConfig {
  String apSsid = "LibationLocker";
  String apPass = ""; // blank=open AP (personal use)

  // data paths (unused when persistence disabled)
  String inventoryPath = "/data/inventory.json";
  String configPath    = "/data/config.json";
};

class AppConfig {
public:
  static void begin();
  static const RuntimeConfig& get();
private:
  static RuntimeConfig _cfg;
};
