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
#include <functional>
#include "Models.h"
#include "PsramAllocator.h"

// Forward-declare Print so callers don't need to include it
class Print;

// The inventory vector's backing array lives in PSRAM to keep
// ~10-15 KB off the internal heap. Individual Item String buffers
// still allocate on internal heap (Arduino String uses malloc).
using ItemVector = std::vector<Item, PsramStdAllocator<Item>>;

struct ImportDryRun {
  int addCount = 0;
  int updateCount = 0;
  int conflictCount = 0;
};

class InventoryStore {
public:
  static void begin();

  static String lastError();

  // ---- Read access (thread-safe) ----

  // Returns a standard-allocator copy (internal heap) for callers that
  // need an independent snapshot. Prefer forEach() for iteration to
  // avoid the copy overhead.
  static std::vector<Item> getAll();

  // Item count without copying the vector
  static size_t count();

  // Iterate items under lock. Callback receives a const reference.
  // Return false from the callback to stop early.
  static void forEach(std::function<bool(const Item&)> cb);

  static bool getById(const String& id, Item& out);

  // ---- Write access ----

  static bool create(const Item& in, Item& outCreated);

  // Update with optimistic concurrency
  static bool update(const String& id,
                     const Item& in,
                     Item& outUpdated,
                     bool& versionConflict,
                     Item& current);

  static bool remove(const String& id);

  // ---- Dropdown config ----

  static DropdownConfig getConfig(); // copy
  static bool setConfig(const DropdownConfig& cfg);

  // ---- Import / Export ----

  // Export full bundle (config + items) as JSON string (PSRAM-backed)
  static bool exportAll(String& outJson);

  // Stream-export: writes JSON directly to a Print sink (AsyncResponseStream, File, etc.)
  // Avoids building the full JSON string in RAM.
  static bool streamExportJson(Print& out);

  static bool importAll(const String& inJson,
                        const String& mode,   // "replace" | "merge" | "append"
                        bool dryRun,
                        ImportDryRun& dr,
                        String& err);

  // Force flush pending saves
  static void flushPending(bool flushInventory, bool flushConfig);

  // Background saver task (public so xTaskCreate can reference it)
  static void inventorySaveTask(void* arg);

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

  // Serialize one Item into a small JSON doc and write to a Print sink
  static void _writeItemJson(Print& out, const Item& it);

  // in-memory state (backing array in PSRAM)
  static ItemVector _items;
  static DropdownConfig _cfg;

  // diagnostics
  static String _lastError;

  // serialization lock (protects _items/_cfg + file IO)
  static SemaphoreHandle_t _lock;
};
