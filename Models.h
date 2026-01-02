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
#include <vector>

struct Item {
  String id;
  String type;
  String brand;
  String name;
  int    sizeMl = 750;
  float  abv = NAN; // optional
  int    qty = 0;
  int    remainingPct = 100; // 0..100
  bool   needToBuy = false;
  int    rating = 0; // 0..10
  std::vector<String> tags;
  String notes;

  uint32_t updatedAt = 0; // epoch seconds
  uint32_t version = 0;   // increment on each update
};

struct DropdownConfig {
  std::vector<String> types;
  std::vector<int>    sizesMl;
  std::vector<float>  abvPresets;
  std::vector<int>    remainingPresets;
};
