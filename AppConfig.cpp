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

RuntimeConfig AppConfig::_cfg;

void AppConfig::begin() {
  _cfg = RuntimeConfig();
  _cfg.apSsid = "LibationLocker";
  _cfg.apPass = "";
}

const RuntimeConfig& AppConfig::get() {
  return _cfg;
}
