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

#include "AppWiFi.h"
#include "AppConfig.h"
#include "AppSerial.h"
#include <esp_wifi.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <time.h>
#include <sys/time.h>

// Static member definitions
StaConfig        AppWiFi::_cfg;
uint32_t         AppWiFi::_lastAttemptMs = 0;
bool             AppWiFi::_mdnsStarted   = false;
bool             AppWiFi::_ntpSynced     = false;
bool             AppWiFi::_wasStaConnected = false;
volatile bool    AppWiFi::_pendingReconfig = false;

static Preferences prefs;

static constexpr const char* PREF_NS = "llwifi";
static constexpr const char* K_EN    = "en";
static constexpr const char* K_SSID  = "ssid";
static constexpr const char* K_PASS  = "pass";

// ---------------------------------------------------------------------------
// Private: NVS persistence
// ---------------------------------------------------------------------------

void AppWiFi::load_() {
  prefs.begin(PREF_NS, true);
  _cfg.enabled = prefs.getBool(K_EN, false);
  _cfg.ssid    = prefs.getString(K_SSID, "");
  _cfg.pass    = prefs.getString(K_PASS, "");
  prefs.end();
}

void AppWiFi::save_() {
  prefs.begin(PREF_NS, false);
  prefs.putBool(K_EN,   _cfg.enabled);
  prefs.putString(K_SSID, _cfg.ssid);
  prefs.putString(K_PASS, _cfg.pass);
  prefs.end();
}

// ---------------------------------------------------------------------------
// Private: WiFi bringup — only called from begin() or loop()
// ---------------------------------------------------------------------------

void AppWiFi::startAp_() {
  const auto& cfg = AppConfig::get();

  bool ok = WiFi.softAP(
    cfg.apSsid.c_str(),
    cfg.apPass.length() ? cfg.apPass.c_str() : nullptr,
    1,    // channel
    0,    // hidden SSID (0 = visible)
    4     // max clients
  );

  // Force WPA2 via low-level config (Arduino core sometimes defaults to open)
  wifi_config_t apCfg;
  esp_wifi_get_config(WIFI_IF_AP, &apCfg);
  apCfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  esp_wifi_set_config(WIFI_IF_AP, &apCfg);

  APP_SERIAL.printf("[NET] softAP start: %s\n", ok ? "OK" : "FAIL");
  APP_SERIAL.print("[NET] AP SSID: "); APP_SERIAL.println(cfg.apSsid);
  APP_SERIAL.print("[NET] AP IP:   "); APP_SERIAL.println(WiFi.softAPIP());
}

void AppWiFi::startSta_() {
  if (!_cfg.enabled || !_cfg.ssid.length()) return;

  APP_SERIAL.printf("[NET] STA connect attempt: %s\n", _cfg.ssid.c_str());
  WiFi.setHostname("LibationLocker");
  WiFi.begin(_cfg.ssid.c_str(), _cfg.pass.c_str());
  _lastAttemptMs = millis();
}

// ---------------------------------------------------------------------------
// Private: apply pending reconfiguration (called only from main loop)
// ---------------------------------------------------------------------------

void AppWiFi::applyReconfig_() {
  APP_SERIAL.println("[NET] Applying reconfig...");

  wifi_mode_t targetMode = _cfg.enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP;

  wifi_mode_t currentMode;
  esp_wifi_get_mode(&currentMode);

  if (currentMode != targetMode) {
    // Change mode without destroying the existing softAP interface
    WiFi.mode(targetMode);
    // Important: do NOT call startAp_() again here — that restarts AP and drops clients
  }

  // Clean disconnect (non-blocking, safe even if already disconnected)
  WiFi.disconnect(false);

  if (_cfg.enabled) {
    startSta_();
  }
}

// ---------------------------------------------------------------------------
// Private: mDNS (now starts in both AP and STA)
// ---------------------------------------------------------------------------

void AppWiFi::startMdns_() {
  if (_mdnsStarted) return;

  // AP always has an IP (192.168.4.1), STA has one when connected
  bool hasUsableInterface = (WiFi.status() == WL_CONNECTED) ||
                            (WiFi.getMode() & WIFI_MODE_AP);

  if (!hasUsableInterface) return;

  if (!MDNS.begin("libationlocker")) {
    APP_SERIAL.println("[NET] mDNS start failed");
    return;
  }

  MDNS.addService("http", "tcp", 80);
  _mdnsStarted = true;
  APP_SERIAL.println("[NET] mDNS: libationlocker.local (available in AP + STA)");
}

// ---------------------------------------------------------------------------
// Private: NTP time sync (called once STA connects to a network with internet)
// ---------------------------------------------------------------------------

void AppWiFi::startNtp_() {
  if (_ntpSynced) return;

  // configTime sets up SNTP internally on ESP-IDF.
  // GMT offset and DST are set to 0 — we store UTC epoch and let the
  // client (browser) handle timezone display.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  // Give NTP up to 5 seconds to sync on first connect.
  // After that, the SNTP client continues retrying in the background.
  struct tm timeinfo;
  for (int i = 0; i < 10; ++i) {
    if (getLocalTime(&timeinfo, 500)) {
      _ntpSynced = true;
      time_t now = time(nullptr);
      APP_SERIAL.printf("[NTP] Synced: %lu (UTC)\n", (unsigned long)now);
      return;
    }
  }
  APP_SERIAL.println("[NTP] Initial sync timeout — will retry in background");
}

// ---------------------------------------------------------------------------
// Public: begin
// ---------------------------------------------------------------------------

void AppWiFi::begin() {
  load_();

  WiFi.setSleep(false);

  // Set correct initial mode
  WiFi.mode(_cfg.enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP);

  startAp_();

  if (_cfg.enabled) {
    startSta_();
  }

  // Start mDNS immediately (AP is always running)
  startMdns_();
}

// ---------------------------------------------------------------------------
// Public: loop (main task only)
// ---------------------------------------------------------------------------

void AppWiFi::loop() {
  // Consume any pending STA config change (set from async web handlers)
  if (_pendingReconfig) {
    _pendingReconfig = false;
    applyReconfig_();
  }

  bool connected = (WiFi.status() == WL_CONNECTED);

  // Detect STA connection edge: was disconnected, now connected
  if (connected && !_wasStaConnected) {
    APP_SERIAL.printf("[NET] STA connected: %s  IP: %s\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    startNtp_();
  }
  _wasStaConnected = connected;

  // If NTP hasn't synced yet but STA is connected, check periodically
  if (connected && !_ntpSynced) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
      _ntpSynced = true;
      time_t now = time(nullptr);
      APP_SERIAL.printf("[NTP] Background sync succeeded: %lu (UTC)\n", (unsigned long)now);
    }
  }

  // STA retry logic — every 10 seconds if enabled but not connected
  if (_cfg.enabled && !connected) {
    uint32_t now = millis();
    if (now - _lastAttemptMs >= 10000UL) {
      APP_SERIAL.println("[NET] STA retry...");
      WiFi.disconnect(false);
      startSta_();
      _lastAttemptMs = now;
    }
  }

  // Safety net: restart mDNS if it ever stops (rare)
  if (!_mdnsStarted) {
    startMdns_();
  }
}

// ---------------------------------------------------------------------------
// Public: safe setters (called from any task → defer to loop)
// ---------------------------------------------------------------------------

bool AppWiFi::setStaConfig(const StaConfig& cfg) {
  _cfg = cfg;
  save_();
  _pendingReconfig = true;
  return true;
}

void AppWiFi::reconnect() {
  _pendingReconfig = true;
}

// ---------------------------------------------------------------------------
// Public: status accessors (thread-safe read-only)
// ---------------------------------------------------------------------------

String AppWiFi::apSsid() {
  return AppConfig::get().apSsid;
}

IPAddress AppWiFi::apIP() {
  return WiFi.softAPIP();
}

bool AppWiFi::staConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String AppWiFi::staSsid() {
  return WiFi.SSID();
}

IPAddress AppWiFi::staIP() {
  return WiFi.localIP();
}

int AppWiFi::staRssi() {
  return staConnected() ? WiFi.RSSI() : 0;
}

StaConfig AppWiFi::getStaConfig() {
  return _cfg;
}

// ---------------------------------------------------------------------------
// NTP accessors
// ---------------------------------------------------------------------------

bool AppWiFi::ntpSynced() {
  return _ntpSynced;
}

uint32_t AppWiFi::epochNow() {
  if (!_ntpSynced) return 0;
  time_t now = time(nullptr);
  // Sanity: if time() returns something before 2024, NTP hasn't really synced
  if (now < 1704067200UL) return 0;  // 2024-01-01 00:00:00 UTC
  return (uint32_t)now;
}