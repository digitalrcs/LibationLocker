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

#include <Preferences.h>
#include <ESPmDNS.h>

StaConfig AppWiFi::_cfg;
uint32_t AppWiFi::_lastAttemptMs = 0;
bool AppWiFi::_mdnsStarted = false;

static Preferences prefs;

static constexpr const char* PREF_NS = "llwifi";
static constexpr const char* K_EN    = "en";
static constexpr const char* K_SSID  = "ssid";
static constexpr const char* K_PASS  = "pass";

void AppWiFi::load_() {
  prefs.begin(PREF_NS, true);
  _cfg.enabled = prefs.getBool(K_EN, false);
  _cfg.ssid    = prefs.getString(K_SSID, "");
  _cfg.pass    = prefs.getString(K_PASS, "");
  prefs.end();
}

void AppWiFi::save_() {
  prefs.begin(PREF_NS, false);
  prefs.putBool(K_EN, _cfg.enabled);
  prefs.putString(K_SSID, _cfg.ssid);
  prefs.putString(K_PASS, _cfg.pass);
  prefs.end();
}

void AppWiFi::startAp_() {
  const auto& cfg = AppConfig::get();
  // AP should never disable
  bool ok = WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.length() ? cfg.apPass.c_str() : nullptr);
  APP_SERIAL.printf("[NET] softAP start: %s\n", ok ? "OK" : "FAIL");
  APP_SERIAL.print("[NET] AP SSID: "); APP_SERIAL.println(cfg.apSsid);
  APP_SERIAL.print("[NET] AP IP:   "); APP_SERIAL.println(WiFi.softAPIP());
}

void AppWiFi::startSta_() {
  if (!_cfg.enabled || !_cfg.ssid.length()) return;
  APP_SERIAL.printf("[NET] STA connect: %s\n", _cfg.ssid.c_str());
  WiFi.setHostname("LibationLocker");
  WiFi.begin(_cfg.ssid.c_str(), _cfg.pass.c_str());
  _lastAttemptMs = millis();
}

void AppWiFi::begin() {
  load_();

  WiFi.setSleep(false);
  // Always allow AP; if STA enabled we do AP+STA, else AP only
  WiFi.mode(_cfg.enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP);

  startAp_();

  if (_cfg.enabled) {
    startSta_();
  }
}

void AppWiFi::loop() {
  // If enabled but not connected, retry every 10s
  if (_cfg.enabled && WiFi.status() != WL_CONNECTED) {
    const uint32_t now = millis();
    if (now - _lastAttemptMs > 10000) {
      APP_SERIAL.println("[NET] STA retry...");
      WiFi.disconnect(true /*wifioff*/);
      delay(10);
      WiFi.mode(WIFI_MODE_APSTA);
      startAp_();
      startSta_();
    }
  }

  // Start/stop mDNS based on STA connection state.
  if (WiFi.status() == WL_CONNECTED) {
    startMdns_();
  } else if (_mdnsStarted) {
    MDNS.end();
    _mdnsStarted = false;
  }

}

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
  if (!staConnected()) return 0;
  return WiFi.RSSI();
}

StaConfig AppWiFi::getStaConfig() {
  return _cfg;
}

bool AppWiFi::setStaConfig(const StaConfig& cfg) {
  _cfg = cfg;
  save_();

  // Apply mode immediately
  WiFi.mode(_cfg.enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP);
  startAp_();
  reconnect();
  return true;
}

void AppWiFi::reconnect() {
  if (!_cfg.enabled) {
    WiFi.disconnect(false);
    return;
  }
  WiFi.disconnect(false);
  delay(10);
  startSta_();
}


void AppWiFi::startMdns_() {
  if (_mdnsStarted) return;
  if (WiFi.status() != WL_CONNECTED) return;

  // Start mDNS so you can browse to http://libationlocker.local/
  if (!MDNS.begin("libationlocker")) {
    APP_SERIAL.println("[NET] mDNS start failed");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  _mdnsStarted = true;
  APP_SERIAL.println("[NET] mDNS: libationlocker.local");
}
