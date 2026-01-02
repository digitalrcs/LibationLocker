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
#include "Models.h"

struct ImportDryRun {
  int addCount = 0;
  int updateCount = 0;
  int conflictCount = 0;
};

class InventoryStore {
public:
  static void begin();

  // last persistence error (best-effort; primarily for HTTP error reporting)
  static String lastError();

  // CRUD
  static std::vector<Item> getAll(); // copy (safe)
  static bool getById(const String& id, Item& out);

  static bool create(const Item& in, Item& outCreated);

  // update with optimistic concurrency:
  // - versionConflict=true when 'in.version' doesn't match current item version
  // - 'current' is filled with current stored item when conflict occurs
  static bool update(const String& id,
                     const Item& in,
                     Item& outUpdated,
                     bool& versionConflict,
                     Item& current);

  static bool remove(const String& id);

  // dropdown config
  static DropdownConfig getConfig(); // copy
  static bool setConfig(const DropdownConfig& cfg);

  // import/export bundle (config + items)
  static bool exportAll(String& outJson);
  static bool importAll(const String& inJson,
                        const String& mode,   // "replace" | "merge" (default) | "append"
                        bool dryRun,
                        ImportDryRun& dr,
                        String& err);

private:
  static String _inventoryPath();
  static String _configPath();

  static bool _ensureFs();
  static void _seedDefaultsIfMissing();

  static bool _loadInventory();
  static bool _saveInventory();
  static bool _loadConfig();
  static bool _saveConfig();

  static String _genId();
  static uint32_t _now();

  // in-memory state
  static std::vector<Item> _items;
  static DropdownConfig _cfg;

  // diagnostics
  static String _lastError;

  // serialization lock (protects _items/_cfg + file IO)
  static SemaphoreHandle_t _lock;
};
