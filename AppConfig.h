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
  // IMPORTANT: For safety when sharing the device, do NOT leave this blank.
  // If blank at boot, AppConfig::begin() will use the compiled-in default.
  String apPass = "adminadmin";
// HTTP Basic Auth for UI + API.
  // If webUser is blank, auth is disabled. Default is admin/admin.
  // If webUser is set, requests must include valid Basic Auth credentials.
  String webUser = "admin";
  String webPass = "admin";
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
