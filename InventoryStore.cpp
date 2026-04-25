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
#include "PsramAllocator.h"
#include "AppConfig.h"
#include "AppWiFi.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <functional>
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <esp_task_wdt.h>
#include "AppSerial.h"

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

ItemVector             InventoryStore::_items;
DropdownConfig         InventoryStore::_cfg;
SemaphoreHandle_t      InventoryStore::_lock = nullptr;
String                 InventoryStore::_lastError;

static bool               s_fsReady = false;
static EventGroupHandle_t s_saveEvents = nullptr;
static TaskHandle_t       s_saveTaskHandle = nullptr;

static constexpr EventBits_t EVT_INV_DIRTY = 1 << 0;
static constexpr EventBits_t EVT_CFG_DIRTY = 1 << 1;

// Rough per-item JSON size for buffer estimation.
// Covers: id(10) + type(15) + brand(25) + name(30) + sizeMl(5) + abv(5)
//         + qty(3) + remainingPct(3) + needToBuy(5) + rating(2) + tags(40)
//         + notes(80) + updatedAt(12) + version(3) + keys/punctuation(~120)
// Total ~360 bytes. Use 512 to leave headroom for long notes/tags.
static constexpr size_t EST_BYTES_PER_ITEM = 512;

// ---------------------------------------------------------------------------
// Lock helpers
// ---------------------------------------------------------------------------

static void withLock(SemaphoreHandle_t lock, std::function<void()> fn) {
  if (!lock) { fn(); return; }
  if (xSemaphoreTakeRecursive(lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
    fn();
    xSemaphoreGiveRecursive(lock);
  } else {
    APP_SERIAL.println("[INV] Lock timeout (2000 ms)");
  }
}

static bool withLockBool(SemaphoreHandle_t lock, std::function<bool()> fn) {
  if (!lock) return fn();
  if (xSemaphoreTakeRecursive(lock, pdMS_TO_TICKS(2000)) == pdTRUE) {
    bool ok = fn();
    xSemaphoreGiveRecursive(lock);
    return ok;
  }
  APP_SERIAL.println("[INV] Lock timeout (2000 ms)");
  return false;
}

// ---------------------------------------------------------------------------
// Background saver task
// ---------------------------------------------------------------------------

void InventoryStore::inventorySaveTask(void* arg) {
  for (;;) {
    EventBits_t bits = xEventGroupWaitBits(s_saveEvents,
                                           EVT_INV_DIRTY | EVT_CFG_DIRTY,
                                           pdTRUE,
                                           pdFALSE,
                                           portMAX_DELAY);

    // Debounce: wait 800ms so rapid edits coalesce into one write
    vTaskDelay(pdMS_TO_TICKS(800));

    // Track which saves succeed so we can re-mark failures
    bool cfgOk = true, invOk = true;

    if (xSemaphoreTakeRecursive(_lock, pdMS_TO_TICKS(3000)) == pdTRUE) {
      if (bits & EVT_CFG_DIRTY) {
        cfgOk = _saveConfig();
        if (!cfgOk) APP_SERIAL.println("[INV] Config save FAILED — will retry");
      }
      if (bits & EVT_INV_DIRTY) {
        invOk = _saveInventory();
        if (!invOk) APP_SERIAL.println("[INV] Inventory save FAILED — will retry");
      }
      xSemaphoreGiveRecursive(_lock);
    } else {
      APP_SERIAL.println("[INV] Save task lock timeout (3000 ms) — will retry");
      cfgOk = false;
      invOk = false;
    }

    // Re-set dirty bits for anything that failed so it retries next cycle
    EventBits_t retry = 0;
    if (!cfgOk && (bits & EVT_CFG_DIRTY)) retry |= EVT_CFG_DIRTY;
    if (!invOk && (bits & EVT_INV_DIRTY)) retry |= EVT_INV_DIRTY;
    if (retry) xEventGroupSetBits(s_saveEvents, retry);
  }
}

static void ensureSaveTaskStarted() {
  if (!s_saveEvents) s_saveEvents = xEventGroupCreate();
  if (!s_saveTaskHandle) {
    xTaskCreatePinnedToCore(
      InventoryStore::inventorySaveTask,
      "inv_save",
      12288,    // was 9216 — increased for LittleFS + JSON overhead safety
      nullptr,
      1,
      &s_saveTaskHandle,
      1
    );
  }
}

static void markDirty(EventBits_t bits) {
  ensureSaveTaskStarted();
  xEventGroupSetBits(s_saveEvents, bits);
}

// ---------------------------------------------------------------------------
// begin()
// ---------------------------------------------------------------------------

void InventoryStore::begin() {
  if (!_lock) _lock = xSemaphoreCreateRecursiveMutex();
  ensureSaveTaskStarted();

  if (!_ensureFs()) {
    APP_SERIAL.println("[INV] LittleFS mount failed - RAM only");
  }

  _seedDefaultsIfMissing();
  _loadConfig();
  _loadInventory();

  APP_SERIAL.printf("[INV] Startup config: types=%u sizes=%u abv=%u rem=%u\n",
                    _cfg.types.size(), _cfg.sizesMl.size(),
                    _cfg.abvPresets.size(), _cfg.remainingPresets.size());
  APP_SERIAL.printf("[INV] Loaded %u items (capacity=%u, PSRAM-backed)\n",
                    _items.size(), _items.capacity());
  APP_SERIAL.printf("[INV] Free heap: %u  PSRAM free: %u\n",
                    ESP.getFreeHeap(), ESP.getFreePsram());
}

String InventoryStore::lastError() {
  String e;
  withLock(_lock, [&](){ e = _lastError; });
  return e;
}

// ---------------------------------------------------------------------------
// Read access
// ---------------------------------------------------------------------------

std::vector<Item> InventoryStore::getAll() {
  std::vector<Item> copy;
  withLock(_lock, [&](){
    copy.reserve(_items.size());
    for (const auto& it : _items) copy.push_back(it);
  });
  return copy;
}

size_t InventoryStore::count() {
  size_t n = 0;
  withLock(_lock, [&](){ n = _items.size(); });
  return n;
}

void InventoryStore::forEach(std::function<bool(const Item&)> cb) {
  withLock(_lock, [&]() {
    for (const auto& it : _items) {
      if (!cb(it)) break;
    }
  });
}

bool InventoryStore::getById(const String& id, Item& out) {
  return withLockBool(_lock, [&]() -> bool {
    for (const auto& it : _items) {
      if (it.id == id) { out = it; return true; }
    }
    return false;
  });
}

// ---------------------------------------------------------------------------
// Write access
// ---------------------------------------------------------------------------

bool InventoryStore::create(const Item& in, Item& outCreated) {
  bool ok = withLockBool(_lock, [&]() -> bool {
    Item x = in;
    if (x.id.isEmpty()) x.id = _genId();
    x.updatedAt = _now();
    x.version = 1;
    _items.push_back(x);
    outCreated = x;
    return true;
  });
  if (ok) markDirty(EVT_INV_DIRTY);
  return ok;
}

bool InventoryStore::update(const String& id, const Item& in, Item& outUpdated,
                            bool& versionConflict, Item& current) {
  versionConflict = false;
  bool ok = withLockBool(_lock, [&]() -> bool {
    for (auto& it : _items) {
      if (it.id == id) {
        current = it;
        if (in.version != 0 && in.version != it.version) {
          versionConflict = true;
          return false;
        }
        Item x = in;
        x.id = id;
        x.version = it.version + 1;
        x.updatedAt = _now();
        it = x;
        outUpdated = x;
        return true;
      }
    }
    return false;
  });
  if (ok) markDirty(EVT_INV_DIRTY);
  return ok;
}

bool InventoryStore::remove(const String& id) {
  bool ok = withLockBool(_lock, [&]() -> bool {
    auto it = std::remove_if(_items.begin(), _items.end(),
                             [&](const Item& i){ return i.id == id; });
    if (it != _items.end()) {
      _items.erase(it, _items.end());
      return true;
    }
    return false;
  });
  if (ok) markDirty(EVT_INV_DIRTY);
  return ok;
}

// ---------------------------------------------------------------------------
// Dropdown config
// ---------------------------------------------------------------------------

DropdownConfig InventoryStore::getConfig() {
  DropdownConfig copy;
  withLock(_lock, [&](){ copy = _cfg; });
  return copy;
}

bool InventoryStore::setConfig(const DropdownConfig& cfg) {
  bool ok = withLockBool(_lock, [&](){
    _cfg = cfg;
    return true;
  });
  if (ok) markDirty(EVT_CFG_DIRTY);
  return ok;
}

void InventoryStore::flushPending(bool flushInventory, bool flushConfig) {
  withLock(_lock, [&](){
    if (flushConfig)  _saveConfig();
    if (flushInventory) _saveInventory();
  });
}

// ---------------------------------------------------------------------------
// Per-item JSON serializer (reusable for save, export, streaming)
// Uses a small StaticJsonDocument — safe because it handles one item at a time.
// ---------------------------------------------------------------------------

void InventoryStore::_writeItemJson(Print& out, const Item& it) {
  StaticJsonDocument<1024> doc;
  doc["id"]           = it.id;
  doc["type"]         = it.type;
  doc["brand"]        = it.brand;
  doc["name"]         = it.name;
  doc["sizeMl"]       = it.sizeMl;
  if (!isnan(it.abv)) doc["abv"] = it.abv;
  doc["qty"]          = it.qty;
  doc["remainingPct"] = it.remainingPct;
  doc["needToBuy"]    = it.needToBuy;
  doc["rating"]       = it.rating;
  JsonArray tags = doc.createNestedArray("tags");
  for (const auto& t : it.tags) tags.add(t);
  doc["notes"]        = it.notes;
  doc["updatedAt"]    = it.updatedAt;
  doc["version"]      = it.version;
  serializeJson(doc, out);
}

// ---------------------------------------------------------------------------
// Export (string-based, PSRAM-backed)
// ---------------------------------------------------------------------------

bool InventoryStore::exportAll(String& outJson) {
  return withLockBool(_lock, [&]() -> bool {
    // Allocate in PSRAM: base overhead + per-item estimate
    size_t needed = 4096 + _items.size() * EST_BYTES_PER_ITEM;
    PsramJsonDocument doc(needed);

    doc["app"]     = "LibationLocker";
    doc["version"] = 1;

    // Config block
    JsonObject cfgObj = doc.createNestedObject("config");
    JsonArray types = cfgObj.createNestedArray("types");
    for (const auto& t : _cfg.types) types.add(t);
    JsonArray sizes = cfgObj.createNestedArray("sizesMl");
    for (const auto& s : _cfg.sizesMl) sizes.add(s);
    JsonArray abv = cfgObj.createNestedArray("abvPresets");
    for (const auto& a : _cfg.abvPresets) abv.add(a);
    JsonArray rem = cfgObj.createNestedArray("remainingPresets");
    for (const auto& r : _cfg.remainingPresets) rem.add(r);
    JsonObject ai = cfgObj.createNestedObject("ai");
    ai["enabled"] = _cfg.aiEnabled;
    ai["baseUrl"] = _cfg.aiBaseUrl;
    ai["model"] = _cfg.aiModel;
    ai["apiKey"] = _cfg.aiApiKey;
    ai["systemPrompt"] = _cfg.aiSystemPrompt;
    ai["temperature"] = _cfg.aiTemperature;
    ai["maxTokens"] = _cfg.aiMaxTokens;
    ai["timeoutSec"] = _cfg.aiTimeoutSec;
    ai["disableThinking"] = _cfg.aiDisableThinking;
    ai["streamingEnabled"] = _cfg.aiStreamingEnabled;
    JsonArray modesArr = ai.createNestedArray("modes");
    for (const auto& m : _cfg.aiModes) {
      JsonObject mo = modesArr.createNestedObject();
      mo["id"] = m.id;
      mo["label"] = m.label;
      mo["instruction"] = m.instruction;
      if (m.systemPrompt.length()) mo["systemPrompt"] = m.systemPrompt;
      mo["restrictToInventory"] = m.restrictToInventory;
    }

    // Items array
    JsonArray itemsArr = doc.createNestedArray("items");
    for (const auto& it : _items) {
      JsonObject o = itemsArr.createNestedObject();
      o["id"]           = it.id;
      o["type"]         = it.type;
      o["brand"]        = it.brand;
      o["name"]         = it.name;
      o["sizeMl"]       = it.sizeMl;
      if (!isnan(it.abv)) o["abv"] = it.abv;
      o["qty"]          = it.qty;
      o["remainingPct"] = it.remainingPct;
      o["needToBuy"]    = it.needToBuy;
      o["rating"]       = it.rating;
      JsonArray tags = o.createNestedArray("tags");
      for (const auto& t : it.tags) tags.add(t);
      o["notes"]        = it.notes;
      o["updatedAt"]    = it.updatedAt;
      o["version"]      = it.version;
    }

    if (doc.overflowed()) {
      _lastError = "exportAll: JSON buffer overflow";
      APP_SERIAL.printf("[EXPORT] OVERFLOW — needed %u, capacity %u\n",
                        doc.memoryUsage(), needed);
      return false;
    }

    outJson.clear();
    serializeJson(doc, outJson);
    APP_SERIAL.printf("[EXPORT] exportAll completed (%u bytes, %u items)\n",
                      outJson.length(), _items.size());
    return true;
  });
}

// ---------------------------------------------------------------------------
// Stream-export: writes JSON directly to a Print sink without
// building the full document in RAM. Each item is serialized through
// a small 1024-byte StaticJsonDocument and flushed immediately.
// ---------------------------------------------------------------------------

bool InventoryStore::streamExportJson(Print& out) {
  return withLockBool(_lock, [&]() -> bool {
    out.print("{\"app\":\"LibationLocker\",\"version\":1,");

    // Config
    out.print("\"config\":{");
    {
      out.print("\"types\":[");
      for (size_t i = 0; i < _cfg.types.size(); ++i) {
        if (i) out.print(",");
        out.print("\""); out.print(_cfg.types[i]); out.print("\"");
      }
      out.print("],\"sizesMl\":[");
      for (size_t i = 0; i < _cfg.sizesMl.size(); ++i) {
        if (i) out.print(",");
        out.print(_cfg.sizesMl[i]);
      }
      out.print("],\"abvPresets\":[");
      for (size_t i = 0; i < _cfg.abvPresets.size(); ++i) {
        if (i) out.print(",");
        out.print(_cfg.abvPresets[i], 1);
      }
      out.print("],\"remainingPresets\":[");
      for (size_t i = 0; i < _cfg.remainingPresets.size(); ++i) {
        if (i) out.print(",");
        out.print(_cfg.remainingPresets[i]);
      }
      out.print("]},");
    }

    // Items — one at a time through _writeItemJson
    out.print("\"items\":[");
    for (size_t i = 0; i < _items.size(); ++i) {
      if (i) out.print(",");
      _writeItemJson(out, _items[i]);
      if ((i % 10) == 9) esp_task_wdt_reset();
    }
    out.print("]}");

    APP_SERIAL.printf("[EXPORT] streamExportJson completed (%u items)\n",
                      _items.size());
    return true;
  });
}

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

bool InventoryStore::_ensureFs() {
  if (s_fsReady) return true;
  if (!LittleFS.begin(false)) {
    APP_SERIAL.println("[FS] Mount failed - format attempt");
    if (!LittleFS.begin(true)) {
      _lastError = "LittleFS format+mount failed";
      return false;
    }
  }
  if (!LittleFS.exists("/data")) LittleFS.mkdir("/data");
  s_fsReady = true;
  return true;
}

String InventoryStore::_inventoryPath() { return AppConfig::get().inventoryPath; }
String InventoryStore::_configPath()    { return AppConfig::get().configPath; }

void InventoryStore::_seedDefaultsIfMissing() {
  withLock(_lock, [&](){
    if (_cfg.types.empty()) {
      _cfg.types = {"Bourbon","Rye","Scotch","Whiskey","Tequila","Vodka","Rum","Gin",
                    "Liqueur","Amaro","Wine","Mead","Brandy","Beer","Bitters","Absinthe",
                    "Mixers","Schnapps","Ingredient","Vermouth","NA"};
    }
    if (_cfg.sizesMl.empty()) _cfg.sizesMl = {50,100,118,200,355,375,500,700,750,1000,1500,1750};
    if (_cfg.abvPresets.empty()) _cfg.abvPresets = {5,5.5,6.5,8,9,10,10.5,12.5,12.8,13.5,14,15,16.5,17,20,30,35,40,45,50,55,60};
    if (_cfg.remainingPresets.empty()) _cfg.remainingPresets = {0,10,25,33,50,66,75,90,100};
  });
}

// ---------------------------------------------------------------------------
// Load config (unchanged logic, same small doc)
// ---------------------------------------------------------------------------

bool InventoryStore::_loadConfig() {
  if (!_ensureFs()) {
    APP_SERIAL.println("[CONFIG] FS unavailable -> defaults");
    _seedDefaultsIfMissing();
    return false;
  }

  String path = _configPath();
  if (!LittleFS.exists(path)) {
    APP_SERIAL.printf("[CONFIG] %s missing -> seeding & saving defaults\n", path.c_str());
    _seedDefaultsIfMissing();
    _saveConfig();
    return true;
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    APP_SERIAL.printf("[CONFIG] Cannot open %s\n", path.c_str());
    _seedDefaultsIfMissing();
    return false;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    APP_SERIAL.printf("[CONFIG] Parse error: %s\n", err.c_str());
    _seedDefaultsIfMissing();
    return false;
  }

  DropdownConfig loaded;
  _seedDefaultsIfMissing();
  bool changed = false;

  if (doc["types"].is<JsonArray>()) {
    loaded.types.clear();
    for (JsonVariant v : doc["types"].as<JsonArray>()) {
      String s = v.as<String>();
      if (s.length()) loaded.types.push_back(s);
    }
    APP_SERIAL.printf("[CONFIG] Loaded %u types\n", loaded.types.size());
    changed = true;
  }
  if (doc["sizesMl"].is<JsonArray>()) {
    loaded.sizesMl.clear();
    for (JsonVariant v : doc["sizesMl"].as<JsonArray>()) loaded.sizesMl.push_back(v.as<int>());
    changed = true;
  }
  if (doc["abvPresets"].is<JsonArray>()) {
    loaded.abvPresets.clear();
    for (JsonVariant v : doc["abvPresets"].as<JsonArray>()) loaded.abvPresets.push_back(v.as<float>());
    changed = true;
  }
  if (doc["remainingPresets"].is<JsonArray>()) {
    loaded.remainingPresets.clear();
    for (JsonVariant v : doc["remainingPresets"].as<JsonArray>()) loaded.remainingPresets.push_back(v.as<int>());
    changed = true;
  }

  JsonObject ai = doc["ai"].as<JsonObject>();
  if (!ai.isNull()) {
    loaded.aiEnabled = ai["enabled"] | loaded.aiEnabled;
    loaded.aiBaseUrl = ai["baseUrl"] | loaded.aiBaseUrl;
    loaded.aiModel = ai["model"] | loaded.aiModel;
    loaded.aiApiKey = ai["apiKey"] | loaded.aiApiKey;
    loaded.aiSystemPrompt = ai["systemPrompt"] | loaded.aiSystemPrompt;
    loaded.aiTemperature = ai["temperature"] | loaded.aiTemperature;
    loaded.aiMaxTokens = ai["maxTokens"] | loaded.aiMaxTokens;
    loaded.aiTimeoutSec = ai["timeoutSec"] | loaded.aiTimeoutSec;
    loaded.aiDisableThinking = ai["disableThinking"] | loaded.aiDisableThinking;
    loaded.aiStreamingEnabled = ai["streamingEnabled"] | loaded.aiStreamingEnabled;

    // Load AI modes
    JsonArrayConst modesArr = ai["modes"].as<JsonArrayConst>();
    if (!modesArr.isNull()) {
      loaded.aiModes.clear();
      for (JsonVariantConst mv : modesArr) {
        JsonObjectConst mo = mv.as<JsonObjectConst>();
        if (mo.isNull()) continue;
        AiMode am;
        am.id = mo["id"] | "";
        am.label = mo["label"] | "";
        am.instruction = mo["instruction"] | "";
        am.systemPrompt = mo["systemPrompt"] | "";
        am.restrictToInventory = mo["restrictToInventory"] | true;
        if (am.id.length() && am.label.length()) loaded.aiModes.push_back(am);
      }
    }
    changed = true;
  }

  // Seed default modes if none loaded
  if (loaded.aiModes.empty()) {
    loaded.aiModes = defaultAiModes();
    changed = true;
  }

  if (loaded.aiTemperature < 0.0f) loaded.aiTemperature = 0.0f;
  if (loaded.aiTemperature > 2.0f) loaded.aiTemperature = 2.0f;
  if (loaded.aiMaxTokens < 64) loaded.aiMaxTokens = 64;
  if (loaded.aiMaxTokens > 16384) loaded.aiMaxTokens = 16384;
  if (loaded.aiTimeoutSec < 30) loaded.aiTimeoutSec = 30;
  if (loaded.aiTimeoutSec > 600) loaded.aiTimeoutSec = 600;

  if (changed) {
    withLock(_lock, [&](){ _cfg = loaded; });
    APP_SERIAL.println("[CONFIG] Applied loaded config");
  }
  return changed;
}

bool InventoryStore::_saveConfig() {
  if (!_ensureFs()) return false;

  String path = _configPath();
  String tmp = path + ".tmp";

  File f = LittleFS.open(tmp, "w");
  if (!f) {
    APP_SERIAL.println("[CONFIG] Cannot open temp file");
    return false;
  }

  // IMPORTANT: This was previously StaticJsonDocument<16384> which allocated
  // 16 KB on the STACK. The background save task only has 12 KB of stack,
  // causing a silent stack overflow that killed the task permanently.
  // PsramJsonDocument allocates in PSRAM (or heap fallback), keeping the
  // task stack safe.
  PsramJsonDocument doc(16384);
  withLock(_lock, [&](){
    JsonArray types = doc.createNestedArray("types");
    for (const auto& t : _cfg.types) types.add(t);
    JsonArray sizes = doc.createNestedArray("sizesMl");
    for (const auto& s : _cfg.sizesMl) sizes.add(s);
    JsonArray abv = doc.createNestedArray("abvPresets");
    for (const auto& a : _cfg.abvPresets) abv.add(a);
    JsonArray rem = doc.createNestedArray("remainingPresets");
    for (const auto& r : _cfg.remainingPresets) rem.add(r);
    JsonObject ai = doc.createNestedObject("ai");
    ai["enabled"] = _cfg.aiEnabled;
    ai["baseUrl"] = _cfg.aiBaseUrl;
    ai["model"] = _cfg.aiModel;
    ai["apiKey"] = _cfg.aiApiKey;
    ai["systemPrompt"] = _cfg.aiSystemPrompt;
    ai["temperature"] = _cfg.aiTemperature;
    ai["maxTokens"] = _cfg.aiMaxTokens;
    ai["timeoutSec"] = _cfg.aiTimeoutSec;
    ai["disableThinking"] = _cfg.aiDisableThinking;
    ai["streamingEnabled"] = _cfg.aiStreamingEnabled;
    JsonArray modesArr = ai.createNestedArray("modes");
    for (const auto& m : _cfg.aiModes) {
      JsonObject mo = modesArr.createNestedObject();
      mo["id"] = m.id;
      mo["label"] = m.label;
      mo["instruction"] = m.instruction;
      if (m.systemPrompt.length()) mo["systemPrompt"] = m.systemPrompt;
      mo["restrictToInventory"] = m.restrictToInventory;
    }
  });

  if (serializeJson(doc, f) == 0) {
    f.close();
    LittleFS.remove(tmp);
    APP_SERIAL.println("[CONFIG] serializeJson failed");
    return false;
  }

  f.flush();
  f.close();

  if (LittleFS.exists(path)) LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    APP_SERIAL.println("[CONFIG] Rename failed");
    LittleFS.remove(tmp);
    return false;
  }

  APP_SERIAL.println("[CONFIG] Saved");
  return true;
}

// ---------------------------------------------------------------------------
// _loadInventory() — PSRAM-backed, buffer sized to actual file
// ---------------------------------------------------------------------------

bool InventoryStore::_loadInventory() {
  String path = _inventoryPath();
  if (!LittleFS.exists(path)) {
    APP_SERIAL.printf("[INV] No inventory file %s\n", path.c_str());
    return true;
  }

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  // Size the JSON buffer to the file: ArduinoJson needs roughly 2x the
  // raw JSON size for its DOM (keys stored as pointers into the input,
  // plus node overhead). Multiply by 2.5 for safety, minimum 8KB.
  size_t fileSize = f.size();
  size_t docSize = max((size_t)8192, (size_t)(fileSize * 2.5));

  APP_SERIAL.printf("[INV] Loading inventory: file=%u bytes, docBuffer=%u bytes (PSRAM)\n",
                    fileSize, docSize);

  PsramJsonDocument doc(docSize);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    _lastError = String("Inventory parse: ") + err.c_str();
    APP_SERIAL.printf("[INV] Inventory parse error: %s (file=%u, buf=%u)\n",
                      err.c_str(), fileSize, docSize);
    return false;
  }

  JsonArray arr = doc["items"].as<JsonArray>();
  ItemVector loaded;
  loaded.reserve(arr.size() + 20);  // extra capacity avoids realloc on first adds

  for (JsonVariant v : arr) {
    JsonObject o = v.as<JsonObject>();
    Item it;
    it.id           = o["id"] | "";
    it.type         = o["type"] | "";
    it.brand        = o["brand"] | "";
    it.name         = o["name"] | "";
    it.sizeMl       = o["sizeMl"] | 750;
    it.abv          = o.containsKey("abv") ? o["abv"].as<float>() : NAN;
    it.qty          = o["qty"] | 0;
    it.remainingPct = o["remainingPct"] | 100;
    it.needToBuy    = o["needToBuy"] | false;
    it.rating       = o["rating"] | 0;
    it.notes        = o["notes"] | "";
    it.updatedAt    = o["updatedAt"] | 0;
    it.version      = o["version"] | 1;

    if (o.containsKey("tags")) {
      for (JsonVariant t : o["tags"].as<JsonArray>()) it.tags.push_back(t.as<String>());
    }

    if (!it.id.isEmpty()) loaded.push_back(it);
  }

  withLock(_lock, [&](){ _items = std::move(loaded); });
  APP_SERIAL.printf("[INV] Parsed %u items from disk\n", _items.size());
  return true;
}

// ---------------------------------------------------------------------------
// _saveInventory() — STREAMING: writes item-by-item to file.
// No giant JSON document in RAM. Each item uses a 1024-byte StaticJsonDocument
// that is reused for every item. Total RAM cost is constant regardless of
// item count.
// ---------------------------------------------------------------------------

bool InventoryStore::_saveInventory() {
  if (!_ensureFs()) return false;

  String path = _inventoryPath();
  String tmp = path + ".tmp";

  File f = LittleFS.open(tmp, "w");
  if (!f) {
    _lastError = "Cannot open temp file for inventory save";
    APP_SERIAL.println("[INV] Cannot open temp file");
    return false;
  }

  // Write the wrapping structure and items one at a time
  f.print("{\"items\":[");

  for (size_t i = 0; i < _items.size(); ++i) {
    if (i > 0) f.print(",");
    _writeItemJson(f, _items[i]);
    // Feed watchdog every 10 items — LittleFS writes can be slow
    if ((i % 10) == 9) esp_task_wdt_reset();
  }

  f.print("]}");
  f.flush();

  // Verify something was written
  size_t written = f.size();
  f.close();

  if (written < 12) { // minimum valid: {"items":[]}
    _lastError = "Inventory save wrote 0 bytes";
    LittleFS.remove(tmp);
    APP_SERIAL.println("[INV] Save wrote nothing — aborting");
    return false;
  }

  if (LittleFS.exists(path)) LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) {
    _lastError = "Inventory rename failed";
    LittleFS.remove(tmp);
    APP_SERIAL.println("[INV] Rename failed");
    return false;
  }

  APP_SERIAL.printf("[INV] Saved %u items (%u bytes)\n", _items.size(), written);
  return true;
}

// ---------------------------------------------------------------------------
// importAll() — PSRAM-backed parsing
// ---------------------------------------------------------------------------

bool InventoryStore::importAll(const String& inJson,
                               const String& mode,
                               bool dryRun,
                               ImportDryRun& dr,
                               String& err) {
  // Size the document to the input. ArduinoJson needs ~2x for DOM overhead.
  size_t docSize = max((size_t)8192, (size_t)(inJson.length() * 2.5));
  APP_SERIAL.printf("[IMPORT] Parsing %u bytes, docBuffer=%u (PSRAM)\n",
                    inJson.length(), docSize);

  PsramJsonDocument doc(docSize);
  DeserializationError parseErr = deserializeJson(doc, inJson);
  if (parseErr) {
    err = String("JSON parse error: ") + parseErr.c_str();
    APP_SERIAL.printf("[IMPORT] Parse failed: %s\n", parseErr.c_str());
    return false;
  }

  if (doc.overflowed()) {
    err = "JSON too large for available memory";
    APP_SERIAL.printf("[IMPORT] Doc overflowed at %u bytes\n", docSize);
    return false;
  }

  // --- Load config from bundle (if present) ---
  if (!dryRun && doc.containsKey("config")) {
    JsonObject cfgObj = doc["config"].as<JsonObject>();
    DropdownConfig imported;

    if (cfgObj["types"].is<JsonArray>()) {
      for (JsonVariant v : cfgObj["types"].as<JsonArray>()) {
        String s = v.as<String>();
        if (s.length()) imported.types.push_back(s);
      }
    }
    if (cfgObj["sizesMl"].is<JsonArray>()) {
      for (JsonVariant v : cfgObj["sizesMl"].as<JsonArray>())
        imported.sizesMl.push_back(v.as<int>());
    }
    if (cfgObj["abvPresets"].is<JsonArray>()) {
      for (JsonVariant v : cfgObj["abvPresets"].as<JsonArray>())
        imported.abvPresets.push_back(v.as<float>());
    }
    if (cfgObj["remainingPresets"].is<JsonArray>()) {
      for (JsonVariant v : cfgObj["remainingPresets"].as<JsonArray>())
        imported.remainingPresets.push_back(v.as<int>());
    }
    JsonObject ai = cfgObj["ai"].as<JsonObject>();
    if (!ai.isNull()) {
      imported.aiEnabled = ai["enabled"] | imported.aiEnabled;
      imported.aiBaseUrl = ai["baseUrl"] | imported.aiBaseUrl;
      imported.aiModel = ai["model"] | imported.aiModel;
      imported.aiApiKey = ai["apiKey"] | imported.aiApiKey;
      imported.aiSystemPrompt = ai["systemPrompt"] | imported.aiSystemPrompt;
      imported.aiTemperature = ai["temperature"] | imported.aiTemperature;
      imported.aiMaxTokens = ai["maxTokens"] | imported.aiMaxTokens;
      imported.aiTimeoutSec = ai["timeoutSec"] | imported.aiTimeoutSec;
      imported.aiDisableThinking = ai["disableThinking"] | imported.aiDisableThinking;
      imported.aiStreamingEnabled = ai["streamingEnabled"] | imported.aiStreamingEnabled;
      JsonArrayConst modesArr = ai["modes"].as<JsonArrayConst>();
      if (!modesArr.isNull()) {
        imported.aiModes.clear();
        for (JsonVariantConst mv : modesArr) {
          JsonObjectConst mo = mv.as<JsonObjectConst>();
          if (mo.isNull()) continue;
          AiMode am;
          am.id = mo["id"] | "";
          am.label = mo["label"] | "";
          am.instruction = mo["instruction"] | "";
          am.systemPrompt = mo["systemPrompt"] | "";
          am.restrictToInventory = mo["restrictToInventory"] | true;
          if (am.id.length() && am.label.length()) imported.aiModes.push_back(am);
        }
      }
    }

    if (!imported.types.empty() || !imported.sizesMl.empty() ||
        !imported.abvPresets.empty() || !imported.remainingPresets.empty() ||
        imported.aiBaseUrl.length() || imported.aiModel.length() || imported.aiSystemPrompt.length() || imported.aiApiKey.length() || imported.aiEnabled) {
      setConfig(imported);
    }
  }

  // --- Load items ---
  if (!doc.containsKey("items") || !doc["items"].is<JsonArray>()) {
    err = "Missing or invalid 'items' array";
    return false;
  }

  JsonArray arr = doc["items"].as<JsonArray>();
  dr = ImportDryRun{};

  return withLockBool(_lock, [&]() -> bool {

    // "replace" mode: wipe existing inventory
    if (mode == "replace") {
      if (!dryRun) _items.clear();
      for (JsonVariant v : arr) {
        JsonObject o = v.as<JsonObject>();
        Item it;
        it.id           = o["id"].as<String>();
        it.type         = o["type"] | "";
        it.brand        = o["brand"] | "";
        it.name         = o["name"] | "";
        it.sizeMl       = o["sizeMl"] | 750;
        it.abv          = o.containsKey("abv") ? o["abv"].as<float>() : NAN;
        it.qty          = o["qty"] | 0;
        it.remainingPct = o["remainingPct"] | 100;
        it.needToBuy    = o["needToBuy"] | false;
        it.rating       = o["rating"] | 0;
        it.notes        = o["notes"] | "";
        it.updatedAt    = o["updatedAt"] | _now();
        it.version      = o["version"] | 1;

        if (o.containsKey("tags")) {
          for (JsonVariant t : o["tags"].as<JsonArray>())
            it.tags.push_back(t.as<String>());
        }

        if (it.id.isEmpty()) it.id = _genId();
        dr.addCount++;
        if (!dryRun) _items.push_back(it);
      }
    }

    // "merge" or "append"
    else {
      for (JsonVariant v : arr) {
        JsonObject o = v.as<JsonObject>();
        String inId = o["id"].as<String>();

        Item it;
        it.type         = o["type"] | "";
        it.brand        = o["brand"] | "";
        it.name         = o["name"] | "";
        it.sizeMl       = o["sizeMl"] | 750;
        it.abv          = o.containsKey("abv") ? o["abv"].as<float>() : NAN;
        it.qty          = o["qty"] | 0;
        it.remainingPct = o["remainingPct"] | 100;
        it.needToBuy    = o["needToBuy"] | false;
        it.rating       = o["rating"] | 0;
        it.notes        = o["notes"] | "";
        it.updatedAt    = o["updatedAt"] | _now();
        it.version      = o["version"] | 1;

        if (o.containsKey("tags")) {
          for (JsonVariant t : o["tags"].as<JsonArray>())
            it.tags.push_back(t.as<String>());
        }

        // Try to find existing item by id (merge only)
        bool found = false;
        if (mode == "merge" && !inId.isEmpty()) {
          for (auto& existing : _items) {
            if (existing.id == inId) {
              found = true;
              if (it.version != 0 && it.version != existing.version) {
                dr.conflictCount++;
              } else {
                dr.updateCount++;
                if (!dryRun) {
                  it.id = inId;
                  it.version = existing.version + 1;
                  it.updatedAt = _now();
                  existing = it;
                }
              }
              break;
            }
          }
        }

        if (!found) {
          dr.addCount++;
          if (!dryRun) {
            it.id = (mode == "append" || inId.isEmpty()) ? _genId() : inId;
            it.updatedAt = _now();
            _items.push_back(it);
          }
        }
      }
    }

    if (!dryRun) {
      markDirty(EVT_INV_DIRTY);
      APP_SERIAL.printf("[IMPORT] mode=%s add=%d update=%d conflict=%d\n",
                        mode.c_str(), dr.addCount, dr.updateCount, dr.conflictCount);
    }

    return true;
  });
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

String InventoryStore::_genId() {
  uint32_t r = esp_random();
  char buf[16];
  snprintf(buf, sizeof(buf), "%08lx", (unsigned long)r);
  return String(buf);
}

uint32_t InventoryStore::_now() {
  // Use real UTC epoch when NTP is synced (requires STA connection).
  // Falls back to millis-based uptime so items still get timestamps
  // even in AP-only mode.
  uint32_t epoch = AppWiFi::epochNow();
  if (epoch > 0) return epoch;
  return (uint32_t)(millis() / 1000UL);
}
