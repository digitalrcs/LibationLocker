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

#include "InventoryStore.h"
#include "AppConfig.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <functional>

// Forward-declare so we can use it before the definition.
static void withLock(SemaphoreHandle_t lock, std::function<void()> fn);

std::vector<Item> InventoryStore::_items;
DropdownConfig InventoryStore::_cfg;
SemaphoreHandle_t InventoryStore::_lock = nullptr;
String InventoryStore::_lastError;

static bool s_fsReady = false;


String InventoryStore::lastError() {
  String e;
  withLock(_lock, [&](){ e = _lastError; });
  return e;
}

static void withLock(SemaphoreHandle_t lock, std::function<void()> fn) {
  if (!lock) { fn(); return; }
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
    fn();
    xSemaphoreGive(lock);
  } else {
    // If lock can't be acquired, still run to avoid deadlock loops; MVP.
    fn();
  }
}

void InventoryStore::begin() {
  if (!_lock) _lock = xSemaphoreCreateMutex();
  _seedDefaultsIfMissing();
  _loadConfig();
  _loadInventory();
}

std::vector<Item> InventoryStore::getAll() {
  std::vector<Item> copy;
  withLock(_lock, [&](){ copy = _items; });
  return copy;
}

bool InventoryStore::getById(const String& id, Item& out) {
  bool ok=false;
  withLock(_lock, [&](){
    for (auto& it : _items) {
      if (it.id == id) { out = it; ok = true; return; }
    }
  });
  return ok;
}

bool InventoryStore::create(const Item& in, Item& outCreated) {
  bool ok=false;
  withLock(_lock, [&](){
    Item x = in;
    x.id = _genId();
    x.version = 1;
    x.updatedAt = _now();
    _items.push_back(x);
    ok = _saveInventory();
    outCreated = x;
  });
  return ok;
}

bool InventoryStore::update(const String& id, const Item& in, Item& outUpdated, bool& versionConflict, Item& current) {
  bool ok=false;
  versionConflict = false;
  withLock(_lock, [&](){
    for (auto& it : _items) {
      if (it.id == id) {
        current = it;
        // optimistic concurrency
        if (in.version != 0 && in.version != it.version) {
          versionConflict = true;
          ok = false;
          return;
        }

        Item x = in;
        x.id = id;
        x.version = it.version + 1;
        x.updatedAt = _now();
        it = x;

        ok = _saveInventory();
        outUpdated = it;
        return;
      }
    }
    ok = false;
  });
  return ok;
}

bool InventoryStore::remove(const String& id) {
  bool ok=false;
  withLock(_lock, [&](){
    for (size_t i=0;i<_items.size();++i) {
      if (_items[i].id == id) {
        _items.erase(_items.begin()+i);
        ok = _saveInventory();
        return;
      }
    }
    ok = false;
  });
  return ok;
}

DropdownConfig InventoryStore::getConfig() {
  DropdownConfig copy;
  withLock(_lock, [&](){ copy = _cfg; });
  return copy;
}

bool InventoryStore::setConfig(const DropdownConfig& cfg) {
  bool ok=false;
  withLock(_lock, [&](){
    _cfg = cfg;
    ok = _saveConfig();
  });
  return ok;
}

bool InventoryStore::exportAll(String& outJson) {
  bool ok=false;
  withLock(_lock, [&](){
    StaticJsonDocument<8192> doc; // will expand if needed? For larger lists, switch to dynamic doc and stream.
    doc["app"] = "LibationLocker";
    doc["version"] = 1;

    // config
    JsonObject cfg = doc.createNestedObject("config");
    JsonArray types = cfg.createNestedArray("types");
    for (auto& t : _cfg.types) types.add(t);
    JsonArray sizes = cfg.createNestedArray("sizesMl");
    for (auto& s : _cfg.sizesMl) sizes.add(s);
    JsonArray abv = cfg.createNestedArray("abvPresets");
    for (auto& a : _cfg.abvPresets) abv.add(a);
    JsonArray rem = cfg.createNestedArray("remainingPresets");
    for (auto& r : _cfg.remainingPresets) rem.add(r);

    // items
    JsonArray items = doc.createNestedArray("items");
    for (auto& it : _items) {
      JsonObject o = items.createNestedObject();
      o["id"] = it.id;
      o["type"] = it.type;
      o["brand"] = it.brand;
      o["name"] = it.name;
      o["sizeMl"] = it.sizeMl;
      if (!isnan(it.abv)) o["abv"] = it.abv;
      o["qty"] = it.qty;
      o["remainingPct"] = it.remainingPct;
      o["needToBuy"] = it.needToBuy;
      o["rating"] = it.rating;
      JsonArray tags = o.createNestedArray("tags");
      for (auto& tg : it.tags) tags.add(tg);
      o["notes"] = it.notes;
      o["updatedAt"] = it.updatedAt;
      o["version"] = it.version;
    }

    outJson.reserve(8192);
    outJson = "";
    serializeJsonPretty(doc, outJson);
    ok = true;
  });
  return ok;
}

bool InventoryStore::importAll(const String& inJson, const String& mode, bool dryRun, ImportDryRun& dr, String& err) {
  bool ok=false;
  err = "";

  withLock(_lock, [&](){
    DynamicJsonDocument doc(32768);
    auto de = deserializeJson(doc, inJson);
    if (de) { err = String("JSON parse error: ") + de.c_str(); ok=false; return; }

    JsonArray itemsIn = doc["items"].as<JsonArray>();
    if (itemsIn.isNull()) { err = "Missing 'items' array"; ok=false; return; }

    // optional config
    DropdownConfig cfgIn = _cfg;
    if (doc.containsKey("config")) {
      JsonObject c = doc["config"];
      if (c.containsKey("types")) {
        cfgIn.types.clear();
        for (JsonVariant v : c["types"].as<JsonArray>()) cfgIn.types.push_back(v.as<String>());
      }
      if (c.containsKey("sizesMl")) {
        cfgIn.sizesMl.clear();
        for (JsonVariant v : c["sizesMl"].as<JsonArray>()) cfgIn.sizesMl.push_back(v.as<int>());
      }
      if (c.containsKey("abvPresets")) {
        cfgIn.abvPresets.clear();
        for (JsonVariant v : c["abvPresets"].as<JsonArray>()) cfgIn.abvPresets.push_back(v.as<float>());
      }
      if (c.containsKey("remainingPresets")) {
        cfgIn.remainingPresets.clear();
        for (JsonVariant v : c["remainingPresets"].as<JsonArray>()) cfgIn.remainingPresets.push_back(v.as<int>());
      }
    }

    std::vector<Item> newItems;
    newItems.reserve(itemsIn.size());

    auto parseItem = [&](JsonObject o, Item& it)->bool {
      if (!o.containsKey("type") || !o.containsKey("brand") || !o.containsKey("name")) return false;
      it.id = o["id"] | "";
      it.type = o["type"].as<String>();
      it.brand = o["brand"].as<String>();
      it.name = o["name"].as<String>();
      it.sizeMl = o["sizeMl"] | 750;
      if (o.containsKey("abv")) it.abv = o["abv"].as<float>(); else it.abv = NAN;
      it.qty = o["qty"] | 0;
      it.remainingPct = o["remainingPct"] | 100;
      it.needToBuy = o["needToBuy"] | false;
      it.rating = o["rating"] | 0;
      it.tags.clear();
      if (o.containsKey("tags")) {
        for (JsonVariant v : o["tags"].as<JsonArray>()) it.tags.push_back(v.as<String>());
      }
      it.notes = o["notes"] | "";
      it.updatedAt = o["updatedAt"] | _now();
      it.version = o["version"] | 1;
      return true;
    };

    // dry-run accounting helpers
    auto findIdxById = [&](const String& id)->int {
      for (size_t i=0;i<_items.size();++i) if (_items[i].id == id) return (int)i;
      return -1;
    };

    if (mode == "replace") {
      for (JsonVariant v : itemsIn) {
        Item it;
        if (!parseItem(v.as<JsonObject>(), it)) continue;
        if (!it.id.length()) it.id = _genId();
        dr.addCount++;
        newItems.push_back(it);
      }
      if (!dryRun) {
        // Apply + persist atomically: if persistence fails, roll back in-memory changes.
        DropdownConfig oldCfg = _cfg;
        std::vector<Item> oldItems = _items;
        _cfg = cfgIn;
        _items = newItems;
        ok = _saveConfig() && _saveInventory();
        if (!ok) {
          String le = _lastError;
          _cfg = oldCfg;
          _items = oldItems;
          err = le.length() ? le : "save_failed";
        }
      } else ok = true;
      return;
    }

    // merge or append
    std::vector<Item> merged = _items;

    for (JsonVariant v : itemsIn) {
      Item it;
      if (!parseItem(v.as<JsonObject>(), it)) continue;
      if (!it.id.length()) {
        // append new
        it.id = _genId();
        it.version = 1;
        it.updatedAt = _now();
        dr.addCount++;
        merged.push_back(it);
        continue;
      }

      int idx = findIdxById(it.id);
      if (idx < 0) {
        dr.addCount++;
        merged.push_back(it);
      } else {
        if (mode == "append") {
          // treat as new
          it.id = _genId();
          it.version = 1;
          it.updatedAt = _now();
          dr.addCount++;
          merged.push_back(it);
        } else {
          // merge: overwrite existing; increment version
          dr.updateCount++;
          it.version = merged[idx].version + 1;
          it.updatedAt = _now();
          merged[idx] = it;
        }
      }
    }

    if (!dryRun) {
      // Apply + persist atomically: if persistence fails, roll back in-memory changes.
      DropdownConfig oldCfg = _cfg;
      std::vector<Item> oldItems = _items;
      _cfg = cfgIn;
      _items = merged;
      ok = _saveConfig() && _saveInventory();
      if (!ok) {
        String le = _lastError;
        _cfg = oldCfg;
        _items = oldItems;
        err = le.length() ? le : "save_failed";
      }
    } else ok = true;
  });

  return ok;
}


bool InventoryStore::_ensureFs() {
  if (s_fsReady) return true;

  // With the partition label standardized to "spiffs" (subtype littlefs),
  // we can use the default LittleFS.begin() behavior.
  if (!LittleFS.begin(false)) {
    if (!LittleFS.begin(true)) {
      withLock(_lock, [&](){
        _lastError =
          "LittleFS mount failed (begin(false) and begin(true) both failed). "
          "Confirm your partition scheme includes a data,littlefs partition named 'spiffs' "
          "and try Tools->Erase Flash: All Flash Contents once.";
      });
      return false;
    }
  }

  // Ensure base folder exists.
  if (!LittleFS.exists("/data")) {
    LittleFS.mkdir("/data");
  }

  s_fsReady = true;
  return true;
}


String InventoryStore::_inventoryPath() { return AppConfig::get().inventoryPath; }
String InventoryStore::_configPath() { return AppConfig::get().configPath; }

void InventoryStore::_seedDefaultsIfMissing() {
  // Ensure FS is mounted and our data directory exists.
  if (!_ensureFs()) return;

  // Best-effort create data directory
  if (!LittleFS.exists("/data")) {
    LittleFS.mkdir("/data");
  }

  // If files don't exist yet, write defaults so the UI has something to work with
  if (!LittleFS.exists(_configPath())) {
    _loadConfig();   // loads defaults into _cfg
    _saveConfig();   // persists defaults
  }
  if (!LittleFS.exists(_inventoryPath())) {
    withLock(_lock, [&](){ _items.clear(); });
    _saveInventory();
  }
}

bool InventoryStore::_loadInventory() {
  if (!_ensureFs()) return false;

  const String path = _inventoryPath();
  if (!LittleFS.exists(path)) {
    withLock(_lock, [&](){ _items.clear(); });
    return true;
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    withLock(_lock, [&](){ _lastError = "open(read) failed: " + path; });
    return false;
  }

  // Capacity heuristic: ~2x file size + a little. Cap at 1MB to avoid runaway allocations.
  size_t cap = (size_t)max((uint32_t)4096, (uint32_t)f.size() * 2U + 2048U);
  cap = (size_t)min((uint32_t)cap, (uint32_t)(1024U * 1024U));

  DynamicJsonDocument doc(cap);
  DeserializationError de = deserializeJson(doc, f);
  f.close();

  if (de) {
    withLock(_lock, [&](){ _lastError = String("JSON parse error (inventory): ") + de.c_str(); });
    return false;
  }

  JsonArray arr;
  if (doc.is<JsonArray>()) {
    arr = doc.as<JsonArray>();
  } else if (doc.containsKey("items")) {
    arr = doc["items"].as<JsonArray>();
  }

  if (arr.isNull()) {
    withLock(_lock, [&](){ _lastError = "Inventory file missing array"; });
    return false;
  }

  std::vector<Item> loaded;
  loaded.reserve(arr.size());

  for (JsonVariant v : arr) {
    JsonObject o = v.as<JsonObject>();
    if (o.isNull()) continue;

    Item it;
    it.id = o["id"] | "";
    it.type = o["type"] | "";
    it.brand = o["brand"] | "";
    it.name = o["name"] | "";
    it.sizeMl = o["sizeMl"] | 750;
    if (o.containsKey("abv")) it.abv = o["abv"].as<float>(); else it.abv = NAN;
    it.qty = o["qty"] | 0;
    it.remainingPct = o["remainingPct"] | 100;
    it.needToBuy = o["needToBuy"] | false;
    it.rating = o["rating"] | 0;
    it.tags.clear();
    if (o.containsKey("tags")) {
      for (JsonVariant tv : o["tags"].as<JsonArray>()) it.tags.push_back(tv.as<String>());
    }
    it.notes = o["notes"] | "";
    it.updatedAt = o["updatedAt"] | 0;
    it.version = o["version"] | 0;

    if (it.id.length()) loaded.push_back(it);
  }

  withLock(_lock, [&](){
    _items = loaded;
  });
  return true;
}

bool InventoryStore::_saveInventory() {
  if (!_ensureFs()) return false;

  const String path = _inventoryPath();
  const String tmp  = path + ".tmp";

  // Ensure parent dir exists (best-effort)
  if (!LittleFS.exists("/data")) LittleFS.mkdir("/data");

  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("items");

  withLock(_lock, [&](){
    for (auto& it : _items) {
      JsonObject o = arr.createNestedObject();
      o["id"] = it.id;
      o["type"] = it.type;
      o["brand"] = it.brand;
      o["name"] = it.name;
      o["sizeMl"] = it.sizeMl;
      if (!isnan(it.abv)) o["abv"] = it.abv;
      o["qty"] = it.qty;
      o["remainingPct"] = it.remainingPct;
      o["needToBuy"] = it.needToBuy;
      o["rating"] = it.rating;
      JsonArray tags = o.createNestedArray("tags");
      for (auto& tg : it.tags) tags.add(tg);
      o["notes"] = it.notes;
      o["updatedAt"] = it.updatedAt;
      o["version"] = it.version;
    }
  });

  File f = LittleFS.open(tmp, "w");
  if (!f) {
    withLock(_lock, [&](){ _lastError = "open(write) failed: " + tmp; });
    return false;
  }

  if (serializeJson(doc, f) == 0) {
    f.close();
    withLock(_lock, [&](){ _lastError = "serialize failed: " + tmp; });
    return false;
  }
  f.flush();
  f.close();

  // Atomic-ish replace
  if (LittleFS.exists(path)) LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    // If rename fails, try copy fallback
    File src = LittleFS.open(tmp, "r");
    File dst = LittleFS.open(path, "w");
    if (!src || !dst) {
      if (src) src.close();
      if (dst) dst.close();
      withLock(_lock, [&](){ _lastError = "rename failed and copy fallback failed"; });
      return false;
    }
    uint8_t buf[512];
    while (true) {
      int n = src.read(buf, sizeof(buf));
      if (n <= 0) break;
      dst.write(buf, (size_t)n);
    }
    src.close(); dst.flush(); dst.close();
    LittleFS.remove(tmp);
  }

  return true;
}

bool InventoryStore::_loadConfig() {
  // Defaults first
  DropdownConfig defaults;
  defaults.types = {"Bourbon","Rye","Scotch","Whiskey","Tequila","Vodka","Rum","Gin","Liqueur","Amaro","Wine","Beer","Bitters","Mixers","NA"};
  defaults.sizesMl = {50,200,375,500,700,750,1000,1750};
  defaults.abvPresets = {20,30,35,40,45,50,55,60};
  defaults.remainingPresets = {0,10,25,50,75,100};

  if (!_ensureFs()) {
    withLock(_lock, [&](){ _cfg = defaults; });
    return false;
  }

  const String path = _configPath();
  if (!LittleFS.exists(path)) {
    withLock(_lock, [&](){ _cfg = defaults; });
    return true;
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    withLock(_lock, [&](){ _cfg = defaults; _lastError = "open(read) failed: " + path; });
    return false;
  }

  size_t cap = (size_t)max((uint32_t)2048, (uint32_t)f.size() * 2U + 1024U);
  cap = (size_t)min((uint32_t)cap, (uint32_t)(256U * 1024U));

  DynamicJsonDocument doc(cap);
  DeserializationError de = deserializeJson(doc, f);
  f.close();

  if (de) {
    withLock(_lock, [&](){ _cfg = defaults; _lastError = String("JSON parse error (config): ") + de.c_str(); });
    return false;
  }

  DropdownConfig loaded = defaults;

  if (doc.containsKey("types")) {
    loaded.types.clear();
    for (JsonVariant v : doc["types"].as<JsonArray>()) loaded.types.push_back(v.as<String>());
  }
  if (doc.containsKey("sizesMl")) {
    loaded.sizesMl.clear();
    for (JsonVariant v : doc["sizesMl"].as<JsonArray>()) loaded.sizesMl.push_back(v.as<int>());
  }
  if (doc.containsKey("abvPresets")) {
    loaded.abvPresets.clear();
    for (JsonVariant v : doc["abvPresets"].as<JsonArray>()) loaded.abvPresets.push_back(v.as<float>());
  }
  if (doc.containsKey("remainingPresets")) {
    loaded.remainingPresets.clear();
    for (JsonVariant v : doc["remainingPresets"].as<JsonArray>()) loaded.remainingPresets.push_back(v.as<int>());
  }

  withLock(_lock, [&](){ _cfg = loaded; });
  return true;
}

bool InventoryStore::_saveConfig() {
  if (!_ensureFs()) return false;

  const String path = _configPath();
  const String tmp  = path + ".tmp";

  if (!LittleFS.exists("/data")) LittleFS.mkdir("/data");

  DynamicJsonDocument doc(4096);

  withLock(_lock, [&](){
    JsonArray types = doc.createNestedArray("types");
    for (auto& t : _cfg.types) types.add(t);

    JsonArray sizes = doc.createNestedArray("sizesMl");
    for (auto& s : _cfg.sizesMl) sizes.add(s);

    JsonArray abv = doc.createNestedArray("abvPresets");
    for (auto& a : _cfg.abvPresets) abv.add(a);

    JsonArray rem = doc.createNestedArray("remainingPresets");
    for (auto& r : _cfg.remainingPresets) rem.add(r);
  });

  File f = LittleFS.open(tmp, "w");
  if (!f) {
    withLock(_lock, [&](){ _lastError = "open(write) failed: " + tmp; });
    return false;
  }
  if (serializeJson(doc, f) == 0) {
    f.close();
    withLock(_lock, [&](){ _lastError = "serialize failed: " + tmp; });
    return false;
  }
  f.flush();
  f.close();

  if (LittleFS.exists(path)) LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    withLock(_lock, [&](){ _lastError = "rename failed: " + tmp + " -> " + path; });
    return false;
  }

  return true;
}

String InventoryStore::_genId() {
  // short pseudo-uuid: 8 hex chars + millis low
  uint32_t r = esp_random();
  char buf[16];
  snprintf(buf, sizeof(buf), "%08lx", (unsigned long)r);
  return String(buf);
}

uint32_t InventoryStore::_now() {
  // If time isn't synced, returns millis()/1000. Good enough for personal use.
  return (uint32_t)(millis() / 1000UL);
}