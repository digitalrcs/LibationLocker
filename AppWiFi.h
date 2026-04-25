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

  // NTP time sync (available when STA is connected)
  static bool ntpSynced();
  static uint32_t epochNow();   // returns epoch seconds, or 0 if not synced

  // Config (persisted in NVS)
  static StaConfig getStaConfig();

  // Safe to call from any task (async handler, etc.) — deferred to loop()
  static bool setStaConfig(const StaConfig& cfg);
  static void reconnect();

private:
  static void load_();
  static void save_();
  static void startAp_();
  static void startSta_();
  static void startMdns_();
  static void applyReconfig_();
  static void startNtp_();

  static bool         _mdnsStarted;
  static bool         _ntpSynced;
  static bool         _wasStaConnected;   // edge detection for STA connect
  static StaConfig    _cfg;
  static uint32_t     _lastAttemptMs;

  // Set by setStaConfig()/reconnect() from any task; consumed by loop() in main task.
  // volatile ensures the compiler does not cache the value in a register across tasks.
  static volatile bool _pendingReconfig;
};