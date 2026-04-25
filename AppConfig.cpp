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

#include "AppConfig.h"

#include <Arduino.h>

RuntimeConfig AppConfig::_cfg;

void AppConfig::begin() {
  _cfg = RuntimeConfig();
  _cfg.apSsid = "LibationLocker";

  // Defaults (compile-time in AppConfig.h):
  // - AP password must be at least 8 chars for WPA2. If shorter, force a safe default.
  // - Web UI/API uses HTTP Basic Auth when webUser is non-empty.
  //   Default is admin/admin (change in AppConfig.h before sharing a build).
  if (_cfg.apPass.length() < 8) {
    _cfg.apPass = "adminadmin";
  }
  if (_cfg.webUser.length() && !_cfg.webPass.length()) {
    _cfg.webPass = "admin";
  }
}

const RuntimeConfig& AppConfig::get() {
  return _cfg;
}
