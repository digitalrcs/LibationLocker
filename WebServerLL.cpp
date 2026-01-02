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

#include "WebServerLL.h"
#include <LittleFS.h>
#include "WebUiAssets.h"
#include "AppSerial.h"
#include "AppWiFi.h"
#include "AppConfig.h"
#include "InventoryStore.h"

#include <Arduino.h>
#include <WiFi.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>

static AsyncWebServer server(80);

// -------------------- Helpers --------------------
static void sendJson(AsyncWebServerRequest* req, int code, const String& json) {
  AsyncWebServerResponse* res = req->beginResponse(code, "application/json; charset=utf-8", json);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}
static void sendText(AsyncWebServerRequest* req, int code, const String& text) {
  AsyncWebServerResponse* res = req->beginResponse(code, "text/plain; charset=utf-8", text);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

static void sendTextDownload(AsyncWebServerRequest* req, int code, const String& text, const String& filename) {
  AsyncWebServerResponse* res = req->beginResponse(code, "text/plain; charset=utf-8", text);
  res->addHeader("Cache-Control", "no-store");
  res->addHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  req->send(res);
}
static void sendCsv(AsyncWebServerRequest* req, int code, const String& csv, const String& filename) {
  AsyncWebServerResponse* res = req->beginResponse(code, "text/csv; charset=utf-8", csv);
  res->addHeader("Cache-Control", "no-store");
  res->addHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  req->send(res);
}

static String csvEscape(const String& s) {
  bool need = false;
  for (size_t i=0;i<s.length();++i) {
    const char c = s[i];
    if (c == '"' || c == ',' || c == '\n' || c == '\r') { need = true; break; }
  }
  String out = s;
  out.replace("\"", "\"\"");
  if (need) out = String("\"") + out + "\"";
  return out;
}

static String itemToJson(const Item& it) {
  DynamicJsonDocument doc(1024);
  doc["id"] = it.id;
  doc["type"] = it.type;
  doc["brand"] = it.brand;
  doc["name"] = it.name;
  doc["sizeMl"] = it.sizeMl;
  if (!isnan(it.abv)) doc["abv"] = it.abv;
  doc["qty"] = it.qty;
  doc["remainingPct"] = it.remainingPct;
  doc["needToBuy"] = it.needToBuy;
  doc["rating"] = it.rating;

  JsonArray tags = doc.createNestedArray("tags");
  for (auto& t : it.tags) tags.add(t);

  doc["notes"] = it.notes;
  doc["updatedAt"] = it.updatedAt;
  doc["version"] = it.version;

  String out;
  serializeJson(doc, out);
  return out;
}

static bool parseItemJson(const String& body, Item& out, String& err) {
  DynamicJsonDocument doc(2048);
  DeserializationError e = deserializeJson(doc, body);
  if (e) { err = "bad_json"; return false; }

  if (!doc.containsKey("type"))  { err = "missing_type";  return false; }
  if (!doc.containsKey("brand")) { err = "missing_brand"; return false; }
  if (!doc.containsKey("name"))  { err = "missing_name";  return false; }

  out.id = doc["id"] | "";
  out.type  = (const char*)doc["type"];
  out.brand = (const char*)doc["brand"];
  out.name  = (const char*)doc["name"];
  out.sizeMl = doc["sizeMl"] | 750;
  out.abv = doc.containsKey("abv") ? (float)doc["abv"].as<float>() : NAN;
  out.qty = doc["qty"] | 0;
  out.remainingPct = doc["remainingPct"] | 100;
  out.needToBuy = doc["needToBuy"] | false;
  out.rating = doc["rating"] | 0;

  out.tags.clear();
  if (doc.containsKey("tags")) {
    for (JsonVariant v : doc["tags"].as<JsonArray>()) out.tags.push_back((const char*)v.as<const char*>());
  }
  out.notes = doc["notes"] | "";

  out.updatedAt = doc["updatedAt"] | 0;
  out.version   = doc["version"]   | 0;
  return true;
}

static String configToJson(const DropdownConfig& cfg) {
  DynamicJsonDocument doc(4096);
  JsonArray aTypes = doc.createNestedArray("types");
  for (auto& t : cfg.types) aTypes.add(t);

  JsonArray aSizes = doc.createNestedArray("sizesMl");
  for (auto v : cfg.sizesMl) aSizes.add(v);

  JsonArray aAbv = doc.createNestedArray("abvPresets");
  for (auto v : cfg.abvPresets) aAbv.add(v);

  JsonArray aRem = doc.createNestedArray("remainingPresets");
  for (auto v : cfg.remainingPresets) aRem.add(v);

  String out;
  serializeJson(doc, out);
  return out;
}

static bool parseConfigJson(const String& body, DropdownConfig& out, String& err) {
  DynamicJsonDocument doc(4096);
  DeserializationError e = deserializeJson(doc, body);
  if (e) { err = "bad_json"; return false; }

  out.types.clear();
  out.sizesMl.clear();
  out.abvPresets.clear();
  out.remainingPresets.clear();

  if (doc.containsKey("types")) for (JsonVariant v : doc["types"].as<JsonArray>()) out.types.push_back((const char*)v.as<const char*>());
  if (doc.containsKey("sizesMl")) for (JsonVariant v : doc["sizesMl"].as<JsonArray>()) out.sizesMl.push_back((int)v.as<int>());
  if (doc.containsKey("abvPresets")) for (JsonVariant v : doc["abvPresets"].as<JsonArray>()) out.abvPresets.push_back((float)v.as<float>());
  if (doc.containsKey("remainingPresets")) for (JsonVariant v : doc["remainingPresets"].as<JsonArray>()) out.remainingPresets.push_back((int)v.as<int>());

  return true;
}

static void handleBodyCollect_(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total, std::function<void(const String&)> onDone) {
  if (index == 0) {
    req->_tempObject = new String();
    ((String*)req->_tempObject)->reserve(total + 1);
  }
  String* body = (String*)req->_tempObject;
  body->concat((const char*)data, len);
  if (index + len == total) {
    String payload = *body;
    delete body;
    req->_tempObject = nullptr;
    onDone(payload);
  }
}

void WebServerLL::begin() {
  // UI: single embedded HTML (inline CSS/JS)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    AsyncWebServerResponse* res = req->beginResponse_P(200, "text/html; charset=utf-8", LL_INDEX_HTML);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  // Health
  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest* req){
    sendJson(req, 200, "{\"ok\":true}");
  });

  // Network status + config
  server.on("/api/net", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(1024);
    doc["mode"] = "AP+optional STA";
    JsonObject ap = doc.createNestedObject("ap");
    ap["ssid"] = AppWiFi::apSsid();
    ap["ip"] = AppWiFi::apIP().toString();

    JsonObject sta = doc.createNestedObject("sta");
    sta["connected"] = AppWiFi::staConnected();
    sta["ssid"] = AppWiFi::staConnected() ? AppWiFi::staSsid() : "";
    sta["ip"] = AppWiFi::staConnected() ? AppWiFi::staIP().toString() : "";
    sta["rssi"] = AppWiFi::staRssi();

    StaConfig cfg = AppWiFi::getStaConfig();
    JsonObject cfgj = doc.createNestedObject("cfg");
    cfgj["enabled"] = cfg.enabled;
    cfgj["ssid"] = cfg.ssid;

    String out;
    serializeJson(doc, out);
    sendJson(req, 200, out);
  });

  server.on("/api/net", HTTP_PUT,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, body)) { sendJson(req, 400, "{\"error\":\"bad_json\"}"); return; }
        StaConfig cfg = AppWiFi::getStaConfig();
        cfg.enabled = doc["enabled"] | cfg.enabled;
        cfg.ssid = (const char*)(doc["ssid"] | cfg.ssid.c_str());
        cfg.pass = (const char*)(doc["pass"] | cfg.pass.c_str());
        AppWiFi::setStaConfig(cfg);
        sendJson(req, 200, "{\"ok\":true}");
      });
    }
  );

  // Storage stats / capacity estimate (LittleFS + heap)
  server.on("/api/storage", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(2048);

    // Heap
    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapFree  = ESP.getFreeHeap();
    doc["heapTotalBytes"] = heapTotal;
    doc["heapUsedBytes"]  = heapTotal - heapFree;
    doc["heapFreeBytes"]  = heapFree;

    // Filesystem (LittleFS)
    // InventoryStore::begin() mounts LittleFS; if it didn't, these may return 0.
    uint32_t fsTotal = (uint32_t)LittleFS.totalBytes();
    uint32_t fsUsed  = (uint32_t)LittleFS.usedBytes();
    uint32_t fsFree  = (fsTotal >= fsUsed) ? (fsTotal - fsUsed) : 0;
    doc["fsTotalBytes"] = fsTotal;
    doc["fsUsedBytes"]  = fsUsed;
    doc["fsFreeBytes"]  = fsFree;
    // Back-compat / UI expects these generic names
    doc["totalBytes"] = fsTotal;
    doc["usedBytes"]  = fsUsed;
    doc["freeBytes"]  = fsFree;
    doc["fsMounted"]  = (fsTotal > 0);


    // Estimate per-item cost based on JSON export length
    String outJson;
    InventoryStore::exportAll(outJson);
    size_t invSize = outJson.length();
    auto items = InventoryStore::getAll();
    size_t n = items.size();
    size_t avg = 260;
    if (n > 0 && invSize > 0) avg = (size_t)max((size_t)200, invSize / n);
    doc["avgBytesPerItem"] = (uint32_t)avg;

    // Items left based on FS space (what actually matters for persistence)
    doc["estItemsLeft"] = avg ? (uint32_t)(fsFree / avg) : 0;

    doc["lastError"] = InventoryStore::lastError();


    String out; serializeJson(doc, out);
    sendJson(req, 200, out);
  });
// Items (stream JSON array)
  server.on("/api/items", HTTP_GET, [](AsyncWebServerRequest* req){
    auto items = InventoryStore::getAll();
    AsyncResponseStream* res = req->beginResponseStream("application/json; charset=utf-8");
    res->addHeader("Cache-Control", "no-store");
    res->print("[");
    for (size_t i=0;i<items.size();++i) {
      if (i) res->print(",");
      res->print(itemToJson(items[i]));
    }
    res->print("]");
    req->send(res);
  });

  // Create item
  server.on("/api/item", HTTP_POST,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        Item it; String err;
        if (!parseItemJson(body, it, err)) { sendJson(req, 400, String("{\"error\":\"") + err + "\"}"); return; }
        Item created;
        if (!InventoryStore::create(it, created)) {
          String e = InventoryStore::lastError();
          if (!e.length()) e = "create_failed";
          sendJson(req, 500, String("{\"error\":\"") + e + "\"}");
          return;
        }
        sendJson(req, 200, itemToJson(created));
      });
    }
  );

  // Update item
  server.on("/api/item", HTTP_PUT,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        Item it; String err;
        if (!parseItemJson(body, it, err)) { sendJson(req, 400, String("{\"error\":\"") + err + "\"}"); return; }
        if (!it.id.length()) { sendJson(req, 400, "{\"error\":\"missing_id\"}"); return; }
        Item outUpdated; Item current; bool versionConflict = false;
        if (!InventoryStore::update(it.id, it, outUpdated, versionConflict, current)) { sendJson(req, 404, "{\"error\":\"not_found\"}"); return; }
        if (versionConflict) { sendJson(req, 409, String("{\"error\":\"version_conflict\",\"current\":") + itemToJson(current) + "}"); return; }
        sendJson(req, 200, itemToJson(outUpdated));
      });
    }
  );

  // Delete item
  server.on("/api/item", HTTP_DELETE, [](AsyncWebServerRequest* req){
    if (!req->hasParam("id")) { sendJson(req, 400, "{\"error\":\"missing_id\"}"); return; }
    String id = req->getParam("id")->value();
    if (!InventoryStore::remove(id)) { sendJson(req, 404, "{\"error\":\"not_found\"}"); return; }
    sendJson(req, 200, "{\"ok\":true}");
  });

  // Dropdown config
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req){
    sendJson(req, 200, configToJson(InventoryStore::getConfig()));
  });

  server.on("/api/config", HTTP_PUT,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        DropdownConfig cfg; String err;
        if (!parseConfigJson(body, cfg, err)) { sendJson(req, 400, String("{\"error\":\"") + err + "\"}"); return; }
        if (!InventoryStore::setConfig(cfg)) { sendJson(req, 500, "{\"error\":\"save_failed\"}"); return; }
        sendJson(req, 200, "{\"ok\":true}");
      });
    }
  );

  
  // Filesystem listing (LittleFS) - lightweight debugging endpoint
  // Returns directory entries for "/" and "/data".
  server.on("/api/fs", HTTP_GET, [](AsyncWebServerRequest* req){
    DynamicJsonDocument doc(8192);

    uint32_t fsTotal = (uint32_t)LittleFS.totalBytes();
    uint32_t fsUsed  = (uint32_t)LittleFS.usedBytes();
    uint32_t fsFree  = (fsTotal >= fsUsed) ? (fsTotal - fsUsed) : 0;

    doc["mounted"] = (fsTotal > 0);
    doc["totalBytes"] = fsTotal;
    doc["usedBytes"]  = fsUsed;
    doc["freeBytes"]  = fsFree;

    JsonArray entries = doc.createNestedArray("entries");

    auto listDir = [&](const char* path){
      File dir = LittleFS.open(path);
      if (!dir || !dir.isDirectory()) return;

      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        JsonObject o = entries.createNestedObject();
        o["name"] = String(f.name());
        o["size"] = (uint32_t)f.size();
        o["isDir"] = f.isDirectory();
      }
    };

    listDir("/");
    listDir("/data");

    String out;
    serializeJson(doc, out);
    sendJson(req, 200, out);
  });

// Export full (json or csv)
  server.on("/api/export", HTTP_GET, [](AsyncWebServerRequest* req){
    String format = "json";
    if (req->hasParam("format")) format = req->getParam("format")->value();

    String filter = "";
    if (req->hasParam("filter")) filter = req->getParam("filter")->value();
    filter.toLowerCase();
    const bool onlyNeed = (filter == "need" || filter == "shopping" || filter == "needtobuy");

    auto items = InventoryStore::getAll();
    if (onlyNeed) {
      std::vector<Item> filtered;
      filtered.reserve(items.size());
      for (auto& it : items) { if (it.needToBuy) filtered.push_back(it); }
      items = std::move(filtered);
    }

    if (format == "txt" || format == "text") {
      String txt;
      txt.reserve(2048);
      if (onlyNeed) {
        txt += "LibationLocker Shopping List (Need=YES)\n";
        txt += "======================================\n\n";
      } else {
        txt += "LibationLocker Export\n";
        txt += "=====================\n\n";
      }

      if (items.empty()) {
        txt += "(none)\n";
      } else {
        for (auto& it : items) {
          txt += "- ";
          if (it.brand.length()) { txt += it.brand; if (it.name.length()) txt += " "; }
          txt += it.name;
          txt += " (" + String(it.sizeMl) + " mL)";
          if (it.type.length()) txt += " [" + it.type + "]";
          txt += "  qty:" + String(it.qty);
          txt += "  rem:" + String(it.remainingPct) + "%";
          if (!isnan(it.abv)) txt += "  abv:" + String(it.abv, 1);
          if (it.tags.size()) {
            txt += "  tags:";
            for (size_t i=0;i<it.tags.size();++i) { if (i) txt += ";"; txt += it.tags[i]; }
          }
          txt += "\n";
        }
      }

      sendTextDownload(req, 200, txt, onlyNeed ? "shopping-list.txt" : "libationlocker-items.txt");
      return;
    }

    if (format == "csv") {
      String csv;
      csv.reserve(4096);
      csv += "id,type,brand,name,sizeMl,abv,qty,remainingPct,needToBuy,rating,tags,notes,updatedAt,version\n";
      for (auto& it : items) {
        String abv = isnan(it.abv) ? "" : String(it.abv, 1);
        String tags;
        for (size_t i=0;i<it.tags.size();++i) { if (i) tags += ";"; tags += it.tags[i]; }
        csv += csvEscape(it.id) + ",";
        csv += csvEscape(it.type) + ",";
        csv += csvEscape(it.brand) + ",";
        csv += csvEscape(it.name) + ",";
        csv += String(it.sizeMl) + ",";
        csv += csvEscape(abv) + ",";
        csv += String(it.qty) + ",";
        csv += String(it.remainingPct) + ",";
        csv += (it.needToBuy ? "YES" : "NO"); csv += ",";
        csv += String(it.rating) + ",";
        csv += csvEscape(tags) + ",";
        csv += csvEscape(it.notes) + ",";
        csv += String(it.updatedAt) + ",";
        csv += String(it.version) + "\n";
      }
      sendCsv(req, 200, csv, onlyNeed ? "shopping-list.csv" : "libationlocker-items.csv");
      return;
    }

    // JSON
    if (!onlyNeed) {
      String out;
      if (!InventoryStore::exportAll(out)) { sendJson(req, 500, "{\"error\":\"export_failed\"}"); return; }
      sendJson(req, 200, out);
      return;
    }

    // Filtered JSON (items only)
    String out;
    out.reserve(1024);
    out = "{\"app\":\"LibationLocker\",\"version\":1,\"filter\":\"need\",\"items\":[";
    for (size_t i=0;i<items.size();++i) {
      out += itemToJson(items[i]);
      if (i + 1 < items.size()) out += ",";
    }
    out += "]}";
    sendJson(req, 200, out);
  });

// Import full list (JSON) - same as before
  server.on("/api/import", HTTP_POST,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        String mode = "merge";
        bool dryRun = false;
        if (req->hasParam("mode")) mode = req->getParam("mode")->value();
        if (req->hasParam("dryrun")) { String v=req->getParam("dryrun")->value(); dryRun = (v=="1"||v=="true"||v=="yes"); }
        ImportDryRun dr; String err;
        bool ok = InventoryStore::importAll(body, mode, dryRun, dr, err);
        if (!ok) { sendJson(req, 400, String("{\"error\":\"") + err + "\"}"); return; }
        DynamicJsonDocument doc(256);
        doc["ok"] = true;
        doc["mode"] = mode;
        doc["dryRun"] = dryRun;
        doc["addCount"] = dr.addCount;
        doc["updateCount"] = dr.updateCount;
        doc["conflictCount"] = dr.conflictCount;
        String out; serializeJson(doc, out);
        sendJson(req, 200, out);
      });
    }
  );

  // Fallback: GET returns app shell for deep links
  server.onNotFound([](AsyncWebServerRequest* req){
    if (req->method() == HTTP_GET) {
      AsyncWebServerResponse* res = req->beginResponse_P(200, "text/html; charset=utf-8", LL_INDEX_HTML);
      res->addHeader("Cache-Control", "no-store");
      req->send(res);
      return;
    }
    req->send(404, "text/plain", "Not Found");
  });

  server.begin();
  APP_SERIAL.println("[WEB] HTTP server started on :80");
}