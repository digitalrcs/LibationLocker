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
#include <WiFi.h>

struct StaConfig {
  bool enabled = false;
  String ssid;
  String pass;
};

class AppWiFi {
public:
  static void begin();
  static void loop();

  // Current status
  static String apSsid();
  static IPAddress apIP();

  static bool staConnected();
  static String staSsid();
  static IPAddress staIP();
  static int staRssi();

  // Config (persisted in NVS)
  static StaConfig getStaConfig();
  static bool setStaConfig(const StaConfig& cfg);

  // Force reconnect attempt (non-blocking)
  static void reconnect();

private:
  static void load_();
  static void save_();
  static void startAp_();
  static void startSta_();
  static void startMdns_();

  static bool _mdnsStarted;

  static StaConfig _cfg;
  static uint32_t _lastAttemptMs;
};
