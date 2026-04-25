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
#include "Favicon.h"
#include "AppSerial.h"
#include "AppWiFi.h"
#include "AppConfig.h"
#include "InventoryStore.h"
#include "AiAssistant.h"
#include "PsramAllocator.h"

#include <Arduino.h>
#include <WiFi.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>

static AsyncWebServer server(80);


struct AiJobState {
  bool busy = false;
  bool done = false;
  bool ok = false;
  String token;
  String kind;
  String answer;
  String error;
  bool inventoryIncluded = false;
  size_t inventoryChars = 0;
  bool inventoryTruncated = false;
  bool inventoryCacheHit = false;
  bool modelValidated = false;
  size_t inventoryItemCount = 0;
  size_t inventoryTotalItems = 0;
  String modeUsed;
  int httpCode = 0;
  uint32_t startedAt = 0;
  uint32_t finishedAt = 0;

  // ---- Streaming / cancellation state ----
  // cancelRequested is read by the AI task on every chunk. The browser
  // sets it via POST /api/ai/cancel. Once the task notices, it closes the
  // upstream socket and returns; cancelled becomes true on the final job
  // state so the UI can distinguish "user stopped" from "model error".
  bool   cancelRequested = false;
  bool   cancelled = false;
  bool   streamed = false;
  String partialAnswer;     // grows as chunks arrive (streaming path only)
  size_t bytesReceived = 0; // raw bytes from upstream socket
  uint32_t lastChunkAt = 0;
};

struct AiTaskArgs {
  String token;
  bool isTest = false;
  AiAskRequest req;
};

static AiJobState g_aiJob;
static SemaphoreHandle_t g_aiJobMutex = nullptr;

static void aiJobEnsureMutex(){
  if (!g_aiJobMutex) g_aiJobMutex = xSemaphoreCreateMutex();
}

static void aiJobResetLocked(){
  g_aiJob = AiJobState();
}

static bool aiJobStart(const AiTaskArgs& args, String& error, String& tokenOut);
static void aiJobTask(void* pv);

static bool aiJobStatusJson(String& out){
  aiJobEnsureMutex();
  if (!xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(200))) return false;
  PsramJsonDocument doc(16384);
  doc["busy"] = g_aiJob.busy;
  doc["done"] = g_aiJob.done;
  doc["ok"] = g_aiJob.ok;
  doc["token"] = g_aiJob.token;
  doc["kind"] = g_aiJob.kind;
  doc["inventoryIncluded"] = g_aiJob.inventoryIncluded;
  doc["inventoryChars"] = (uint32_t)g_aiJob.inventoryChars;
  doc["inventoryTruncated"] = g_aiJob.inventoryTruncated;
  doc["inventoryCacheHit"] = g_aiJob.inventoryCacheHit;
  doc["inventoryItemCount"] = (uint32_t)g_aiJob.inventoryItemCount;
  doc["inventoryTotalItems"] = (uint32_t)g_aiJob.inventoryTotalItems;
  doc["modelValidated"] = g_aiJob.modelValidated;
  doc["modeUsed"] = g_aiJob.modeUsed;
  doc["httpCode"] = g_aiJob.httpCode;
  doc["startedAt"] = g_aiJob.startedAt;
  doc["finishedAt"] = g_aiJob.finishedAt;
  // Streaming / cancellation state
  doc["streamed"] = g_aiJob.streamed;
  doc["cancelRequested"] = g_aiJob.cancelRequested;
  doc["cancelled"] = g_aiJob.cancelled;
  doc["bytesReceived"] = (uint32_t)g_aiJob.bytesReceived;
  doc["lastChunkAt"] = g_aiJob.lastChunkAt;
  // While running, expose partialAnswer so the UI can render progressively.
  // Once done, "answer" holds the final text.
  if (g_aiJob.busy && g_aiJob.partialAnswer.length()) {
    doc["partialAnswer"] = g_aiJob.partialAnswer;
  }
  if (g_aiJob.answer.length()) doc["answer"] = g_aiJob.answer;
  if (g_aiJob.error.length()) doc["error"] = g_aiJob.error;
  serializeJson(doc, out);
  xSemaphoreGive(g_aiJobMutex);
  return true;
}

static bool aiJobStart(const AiTaskArgs& args, String& error, String& tokenOut){
  aiJobEnsureMutex();
  if (!xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(500))) { error = "AI job mutex timeout"; return false; }
  if (g_aiJob.busy) {
    error = "An AI request is already running.";
    tokenOut = g_aiJob.token;
    xSemaphoreGive(g_aiJobMutex);
    return false;
  }
  aiJobResetLocked();
  g_aiJob.busy = true;
  g_aiJob.done = false;
  g_aiJob.ok = false;
  g_aiJob.token = args.token;
  g_aiJob.kind = args.isTest ? "test" : "ask";
  g_aiJob.modeUsed = args.req.mode;
  g_aiJob.startedAt = millis();
  tokenOut = g_aiJob.token;
  xSemaphoreGive(g_aiJobMutex);

  AiTaskArgs* heapArgs = new AiTaskArgs(args);
  if (!heapArgs) {
    if (xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(200))) { aiJobResetLocked(); xSemaphoreGive(g_aiJobMutex); }
    error = "Failed to allocate AI task args";
    return false;
  }
  BaseType_t rc = xTaskCreatePinnedToCore(aiJobTask, "ai_job", 32768, heapArgs, 1, nullptr, 1);
  if (rc != pdPASS) {
    delete heapArgs;
    if (xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(200))) { aiJobResetLocked(); xSemaphoreGive(g_aiJobMutex); }
    error = "Failed to start AI task";
    return false;
  }
  return true;
}

static void aiJobTask(void* pv){
  std::unique_ptr<AiTaskArgs> args((AiTaskArgs*)pv);
  AiAskResponse aiResp;
  String message;
  bool ok = false;

  // ---- Streaming callbacks ----
  // The AiAssistant streaming path invokes these from the same task; the
  // mutex calls below are for cross-task reads (the web request handlers
  // that serve /api/ai/status and /api/ai/cancel).
  AiStreamCallbacks cb;

  cb.shouldCancel = []() -> bool {
    if (!g_aiJobMutex) return false;
    bool requested = false;
    if (xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(50))) {
      requested = g_aiJob.cancelRequested;
      xSemaphoreGive(g_aiJobMutex);
    }
    return requested;
  };

  cb.onChunk = [](const char* delta, size_t deltaLen) {
    if (!g_aiJobMutex || !delta || !deltaLen) return;
    if (xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(50))) {
      // Build a temporary null-terminated buffer on the stack for short
      // deltas, or a heap buffer for longer ones. This avoids relying on
      // String::concat(const char*, unsigned int), whose access modifier
      // varies across Arduino-ESP32 core versions.
      if (deltaLen < 256) {
        char tmp[257];
        memcpy(tmp, delta, deltaLen);
        tmp[deltaLen] = '\0';
        g_aiJob.partialAnswer += tmp;
      } else {
        std::unique_ptr<char[]> tmp(new (std::nothrow) char[deltaLen + 1]);
        if (tmp) {
          memcpy(tmp.get(), delta, deltaLen);
          tmp[deltaLen] = '\0';
          g_aiJob.partialAnswer += tmp.get();
        }
      }
      g_aiJob.lastChunkAt = millis();
      xSemaphoreGive(g_aiJobMutex);
    }
  };

  cb.onProgress = [](size_t answerLen, size_t bytesReceived) {
    (void)answerLen;
    if (!g_aiJobMutex) return;
    if (xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(20))) {
      g_aiJob.bytesReceived = bytesReceived;
      xSemaphoreGive(g_aiJobMutex);
    }
  };

  if (args->isTest) {
    // testConnection() goes through ask() internally and respects the
    // streaming flag, but we don't need progress hooks for the small
    // connection probe.
    ok = AiAssistant::testConnection(message);
    if (ok) aiResp.answer = message; else aiResp.error = message;
    aiResp.modeUsed = "general";
    aiResp.modelValidated = ok;
  } else {
    ok = AiAssistant::askWithCallbacks(args->req, aiResp, cb);
  }

  aiJobEnsureMutex();
  if (xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(1000))) {
    if (g_aiJob.token == args->token) {
      g_aiJob.ok = ok;
      g_aiJob.done = true;
      g_aiJob.busy = false;
      g_aiJob.answer = aiResp.answer;
      g_aiJob.error = aiResp.error;
      g_aiJob.inventoryIncluded = aiResp.inventoryIncluded;
      g_aiJob.inventoryChars = aiResp.inventoryChars;
      g_aiJob.inventoryTruncated = aiResp.inventoryTruncated;
      g_aiJob.inventoryCacheHit = aiResp.inventoryCacheHit;
      g_aiJob.inventoryItemCount = aiResp.inventoryItemCount;
      g_aiJob.inventoryTotalItems = aiResp.inventoryTotalItems;
      g_aiJob.modelValidated = aiResp.modelValidated;
      g_aiJob.modeUsed = aiResp.modeUsed;
      g_aiJob.httpCode = aiResp.httpCode;
      g_aiJob.streamed = aiResp.streamed;
      g_aiJob.cancelled = aiResp.cancelled;
      g_aiJob.bytesReceived = aiResp.bytesReceived;
      g_aiJob.finishedAt = millis();
      // Free the partial buffer — final answer lives in answer now.
      // Keep partialAnswer cleared so the next status poll doesn't return
      // stale streaming text alongside the final answer.
      g_aiJob.partialAnswer = String();
    }
    xSemaphoreGive(g_aiJobMutex);
  }
  vTaskDelete(nullptr);
}

// -------------------- Auth --------------------
static bool requireAuth(AsyncWebServerRequest* req) {
  const auto& cfg = AppConfig::get();
  if (!cfg.webUser.length()) return true;
  if (req->authenticate(cfg.webUser.c_str(), cfg.webPass.c_str())) return true;
  req->requestAuthentication();
  return false;
}

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
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '"' || c == ',' || c == '\n' || c == '\r') { need = true; break; }
  }
  String out = s;
  out.replace("\"", "\"\"");
  if (need) out = String("\"") + out + "\"";
  return out;
}

static String itemToJson(const Item& it) {
  // 1024 bytes: safe for items with long notes (~400 chars) + many tags.
  StaticJsonDocument<1024> doc;
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
  DynamicJsonDocument doc(16384);
  JsonArray aTypes = doc.createNestedArray("types");
  for (auto& t : cfg.types) aTypes.add(t);

  JsonArray aSizes = doc.createNestedArray("sizesMl");
  for (auto v : cfg.sizesMl) aSizes.add(v);

  JsonArray aAbv = doc.createNestedArray("abvPresets");
  for (auto v : cfg.abvPresets) aAbv.add(v);

  JsonArray aRem = doc.createNestedArray("remainingPresets");
  for (auto v : cfg.remainingPresets) aRem.add(v);

  JsonObject ai = doc.createNestedObject("ai");
  ai["enabled"] = cfg.aiEnabled;
  ai["provider"] = cfg.aiProvider;
  ai["baseUrl"] = cfg.aiBaseUrl;
  ai["model"] = cfg.aiModel;
  ai["apiKey"] = cfg.aiApiKey;
  ai["systemPrompt"] = cfg.aiSystemPrompt;
  ai["temperature"] = cfg.aiTemperature;
  ai["maxTokens"] = cfg.aiMaxTokens;
  ai["timeoutSec"] = cfg.aiTimeoutSec;
  ai["disableThinking"] = cfg.aiDisableThinking;
  ai["tlsInsecure"] = cfg.aiTlsInsecure;
  JsonArray aiModesArr = ai.createNestedArray("modes");
  for (const auto& m : cfg.aiModes) {
    JsonObject mo = aiModesArr.createNestedObject();
    mo["id"] = m.id;
    mo["label"] = m.label;
    mo["instruction"] = m.instruction;
    if (m.systemPrompt.length()) mo["systemPrompt"] = m.systemPrompt;
    mo["restrictToInventory"] = m.restrictToInventory;
  }
  JsonArray aiModels = ai.createNestedArray("availableModels");

  String out;
  serializeJson(doc, out);
  return out;
}

static bool parseConfigJson(const String& body, DropdownConfig& out, String& err) {
  DynamicJsonDocument doc(16384);
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

  JsonVariant ai = doc["ai"];
  if (!ai.isNull() && ai.is<JsonObject>()) {
    out.aiEnabled = ai["enabled"] | false;
    String prov = String((const char*)(ai["provider"] | "lmstudio"));
    prov.trim(); prov.toLowerCase();
    // Whitelist; unknown values fall back to lmstudio so persistence never bricks the UI.
    if (prov != "lmstudio" && prov != "openai" &&
        prov != "anthropic" && prov != "openai_compat") {
      prov = "lmstudio";
    }
    out.aiProvider = prov;
    out.aiBaseUrl = String((const char*)(ai["baseUrl"] | ""));
    out.aiModel = String((const char*)(ai["model"] | ""));
    out.aiApiKey = String((const char*)(ai["apiKey"] | ""));
    out.aiSystemPrompt = String((const char*)(ai["systemPrompt"] | ""));
    out.aiTemperature = ai["temperature"] | 0.2f;
    out.aiMaxTokens = ai["maxTokens"] | 8192;
    out.aiTimeoutSec = ai["timeoutSec"] | 180;
    out.aiDisableThinking = ai["disableThinking"] | true;
    out.aiTlsInsecure = ai["tlsInsecure"] | true;

    // Parse modes array
    JsonArrayConst modesArr = ai["modes"].as<JsonArrayConst>();
    if (!modesArr.isNull()) {
      out.aiModes.clear();
      for (JsonVariantConst mv : modesArr) {
        JsonObjectConst mo = mv.as<JsonObjectConst>();
        if (mo.isNull()) continue;
        AiMode am;
        am.id = mo["id"] | "";
        am.label = mo["label"] | "";
        am.instruction = mo["instruction"] | "";
        am.systemPrompt = mo["systemPrompt"] | "";
        am.restrictToInventory = mo["restrictToInventory"] | true;
        if (am.id.length() && am.label.length()) out.aiModes.push_back(am);
      }
    }
  }

  // Seed default modes if none provided
  if (out.aiModes.empty()) out.aiModes = defaultAiModes();

  if (out.aiTemperature < 0.0f) out.aiTemperature = 0.0f;
  if (out.aiTemperature > 2.0f) out.aiTemperature = 2.0f;
  if (out.aiMaxTokens < 64) out.aiMaxTokens = 64;
  if (out.aiMaxTokens > 16384) out.aiMaxTokens = 16384;
  if (out.aiTimeoutSec < 30) out.aiTimeoutSec = 30;
  if (out.aiTimeoutSec > 600) out.aiTimeoutSec = 600;

  return true;
}

static void handleBodyCollect_(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total, std::function<void(const String&)> onDone) {
  if (index == 0) {
    String* buf = new(std::nothrow) String();
    if (!buf) {
      sendJson(req, 503, "{\"error\":\"out_of_memory\"}");
      return;
    }
    buf->reserve(total + 1);
    req->_tempObject = buf;
  }
  if (!req->_tempObject) return;  // allocation failed on first chunk
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
    if (!requireAuth(req)) return;
    AsyncWebServerResponse* res = req->beginResponse_P(200, "text/html; charset=utf-8", LL_INDEX_HTML);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });
  // Favicon
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    AsyncWebServerResponse* res = req->beginResponse_P(200, "image/x-icon", LL_FAVICON_ICO, LL_FAVICON_ICO_LEN);
    res->addHeader("Cache-Control", "public, max-age=86400");
    req->send(res);
  });

  // Health
  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    sendJson(req, 200, "{\"ok\":true}");
  });

  // Network status + config
  server.on("/api/net", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
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
      if (!requireAuth(req)) return;
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

  // Storage stats / capacity estimate
  server.on("/api/storage", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    DynamicJsonDocument doc(2048);

    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapFree  = ESP.getFreeHeap();
    doc["heapTotalBytes"] = heapTotal;
    doc["heapUsedBytes"]  = heapTotal - heapFree;
    doc["heapFreeBytes"]  = heapFree;

    // PSRAM stats (new — useful for monitoring)
    if (psramFound()) {
      doc["psramTotalBytes"] = (uint32_t)ESP.getPsramSize();
      doc["psramFreeBytes"]  = (uint32_t)ESP.getFreePsram();
    }

    uint32_t fsTotal = (uint32_t)LittleFS.totalBytes();
    uint32_t fsUsed  = (uint32_t)LittleFS.usedBytes();
    uint32_t fsFree  = (fsTotal >= fsUsed) ? (fsTotal - fsUsed) : 0;
    doc["fsTotalBytes"] = fsTotal;
    doc["fsUsedBytes"]  = fsUsed;
    doc["fsFreeBytes"]  = fsFree;
    doc["totalBytes"] = fsTotal;
    doc["usedBytes"]  = fsUsed;
    doc["freeBytes"]  = fsFree;
    doc["fsMounted"]  = (fsTotal > 0);

    // Per-item cost estimate from persisted file size
    size_t invSize = 0;
    {
      File f = LittleFS.open("/data/inventory.json", "r");
      if (f) { invSize = (size_t)f.size(); f.close(); }
    }

    // Use count() — no full vector copy
    size_t n = InventoryStore::count();
    size_t avg = 260;
    if (n > 0 && invSize > 0) avg = (size_t)max((size_t)200, invSize / n);
    doc["avgBytesPerItem"] = (uint32_t)avg;
    doc["itemCount"]       = (uint32_t)n;
    doc["estItemsLeft"]    = avg ? (uint32_t)(fsFree / avg) : 0;
    doc["lastError"]       = InventoryStore::lastError();

    String out; serializeJson(doc, out);
    sendJson(req, 200, out);
  });

  // Diagnostics — comprehensive system snapshot
  server.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    DynamicJsonDocument doc(4096);

    // Heap
    doc["heapFree"]      = ESP.getFreeHeap();
    doc["heapTotal"]     = ESP.getHeapSize();
    doc["heapMinFree"]   = ESP.getMinFreeHeap();  // lowest since boot
    doc["heapMaxAlloc"]  = ESP.getMaxAllocHeap();  // largest contiguous block

    // PSRAM
    if (psramFound()) {
      doc["psramFree"]     = (uint32_t)ESP.getFreePsram();
      doc["psramTotal"]    = (uint32_t)ESP.getPsramSize();
      doc["psramMinFree"]  = (uint32_t)ESP.getMinFreePsram();
    }

    // Filesystem
    uint32_t fsTotal = (uint32_t)LittleFS.totalBytes();
    uint32_t fsUsed  = (uint32_t)LittleFS.usedBytes();
    doc["fsFreeBytes"]  = (fsTotal >= fsUsed) ? (fsTotal - fsUsed) : 0;
    doc["fsTotalBytes"] = fsTotal;

    // Inventory file size
    {
      File f = LittleFS.open("/data/inventory.json", "r");
      doc["invFileBytes"] = f ? (uint32_t)f.size() : 0;
      if (f) f.close();
    }

    // Inventory state
    doc["itemCount"]  = (uint32_t)InventoryStore::count();
    doc["lastError"]  = InventoryStore::lastError();

    // Uptime
    doc["uptimeMs"]   = (uint32_t)millis();
    doc["uptimeSec"]  = (uint32_t)(millis() / 1000UL);

    // WiFi
    doc["staConnected"] = AppWiFi::staConnected();
    if (AppWiFi::staConnected()) {
      doc["staRssi"]    = AppWiFi::staRssi();
      doc["staIp"]      = AppWiFi::staIP().toString();
    }
    doc["apIp"] = AppWiFi::apIP().toString();

    // NTP
    doc["ntpSynced"]  = AppWiFi::ntpSynced();
    doc["epochNow"]   = AppWiFi::epochNow();

    // Chip info
    doc["chipModel"]  = ESP.getChipModel();
    doc["chipCores"]  = ESP.getChipCores();
    doc["cpuFreqMhz"] = ESP.getCpuFreqMHz();
    doc["sdkVersion"] = ESP.getSdkVersion();

    String out; serializeJson(doc, out);
    sendJson(req, 200, out);
  });

  // Items (stream JSON array with pagination support)
  // Uses forEach() to iterate items under lock — NO full vector copy.
  // Each item is serialized through a small StaticJsonDocument and written
  // directly to the response stream, keeping heap usage constant regardless
  // of inventory size.
  server.on("/api/items", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    int offset = 0;
    int limit  = -1;
    if (req->hasParam("offset")) offset = req->getParam("offset")->value().toInt();
    if (req->hasParam("limit"))  limit  = req->getParam("limit")->value().toInt();
    if (offset < 0) offset = 0;

    AsyncResponseStream* res = req->beginResponseStream("application/json; charset=utf-8");
    if (!res) {
      sendJson(req, 503, "{\"error\":\"out_of_memory\"}");
      return;
    }
    res->addHeader("Cache-Control", "no-store");
    res->print("[");

    int idx = 0;
    bool first = true;
    InventoryStore::forEach([&](const Item& it) -> bool {
      // Skip items before offset
      if (idx < offset) { ++idx; return true; }
      // Stop after limit items emitted
      if (limit >= 0 && (idx - offset) >= limit) return false;

      if (!first) res->print(",");
      first = false;

      // 1024 bytes: safe for items with long notes (~400 chars) + many tags
      StaticJsonDocument<1024> doc;
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
      for (const auto& t : it.tags) tags.add(t);
      doc["notes"] = it.notes;
      doc["updatedAt"] = it.updatedAt;
      doc["version"] = it.version;
      serializeJson(doc, *res);

      ++idx;

      // Feed the task watchdog every 10 items. This handler runs inside the
      // async_tcp task; serializing 74+ items can exceed the 5-second WDT
      // timeout without this reset.
      if ((idx % 10) == 0) esp_task_wdt_reset();

      return true;
    });

    res->print("]");
    req->send(res);
  });

  // Items count — uses count(), no vector copy
  server.on("/api/items/count", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    size_t n = InventoryStore::count();
    sendJson(req, 200, String("{\"count\":") + (int)n + "}");
  });

  // Create item
  server.on("/api/item", HTTP_POST,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      if (!requireAuth(req)) return;
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        // Guard: refuse if internal heap is critically low
        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < 20000) {
          APP_SERIAL.printf("[WEB] Create refused: heap critically low (%u bytes)\n", freeHeap);
          sendJson(req, 503, "{\"error\":\"heap_low\",\"freeHeap\":" + String(freeHeap) + "}");
          return;
        }
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
      if (!requireAuth(req)) return;
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        Item it; String err;
        if (!parseItemJson(body, it, err)) { sendJson(req, 400, String("{\"error\":\"") + err + "\"}"); return; }
        if (!it.id.length()) { sendJson(req, 400, "{\"error\":\"missing_id\"}"); return; }
        Item outUpdated; Item current; bool versionConflict = false;
        if (!InventoryStore::update(it.id, it, outUpdated, versionConflict, current)) {
          if (versionConflict) {
            sendJson(req, 409, String("{\"error\":\"version_conflict\",\"current\":") + itemToJson(current) + "}");
          } else {
            sendJson(req, 404, "{\"error\":\"not_found\"}");
          }
          return;
        }
        sendJson(req, 200, itemToJson(outUpdated));
      });
    }
  );

  // Delete item
  server.on("/api/item", HTTP_DELETE, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    if (!req->hasParam("id")) { sendJson(req, 400, "{\"error\":\"missing_id\"}"); return; }
    String id = req->getParam("id")->value();
    if (!InventoryStore::remove(id)) { sendJson(req, 404, "{\"error\":\"not_found\"}"); return; }
    sendJson(req, 200, "{\"ok\":true}");
  });

  // Dropdown config
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    sendJson(req, 200, configToJson(InventoryStore::getConfig()));
  });

  server.on("/api/config", HTTP_PUT,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      if (!requireAuth(req)) return;
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        DropdownConfig cfg; String err;
        if (!parseConfigJson(body, cfg, err)) { sendJson(req, 400, String("{\"error\":\"") + err + "\"}"); return; }
        if (!InventoryStore::setConfig(cfg)) { sendJson(req, 500, "{\"error\":\"save_failed\"}"); return; }
        sendJson(req, 200, "{\"ok\":true}");
      });
    }
  );

  // AI model discovery
  server.on("/api/ai/models", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    std::vector<AiModelInfo> models;
    String error;
    PsramJsonDocument outDoc(8192);
    bool ok = AiAssistant::listModels(models, error);
    outDoc["ok"] = ok;
    if (ok) {
      JsonArray arr = outDoc.createNestedArray("models");
      for (const auto& model : models) {
        JsonObject m = arr.createNestedObject();
        m["id"] = model.id;
        m["object"] = model.objectType;
        m["ownedByLmStudio"] = model.ownedByLmStudio;
      }
    } else {
      outDoc["error"] = error;
    }
    String out;
    serializeJson(outDoc, out);
    sendJson(req, ok ? 200 : 500, out);
  });

  // AI ask (async worker to avoid blocking AsyncTCP / watchdog)
  server.on("/api/ai/ask", HTTP_POST,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      if (!requireAuth(req)) return;
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, body)) { sendJson(req, 400, "{\"error\":\"bad_json\"}"); return; }

        AiTaskArgs args;
        args.token = String((uint32_t)millis()) + "-" + String((uint32_t)esp_random(), HEX);
        args.isTest = false;
        args.req.question = String((const char*)(doc["question"] | ""));
        args.req.question.trim();
        args.req.includeInventory = doc["includeInventory"].isNull() ? true : (bool)(doc["includeInventory"] | true);
        args.req.mode = String((const char*)(doc["mode"] | "general"));
        args.req.mode.trim();
        if (!args.req.question.length()) { sendJson(req, 400, "{\"error\":\"missing_question\"}"); return; }

        String error, token;
        bool ok = aiJobStart(args, error, token);
        DynamicJsonDocument outDoc(512);
        outDoc["ok"] = ok;
        if (ok) { outDoc["started"] = true; outDoc["token"] = token; }
        else { outDoc["error"] = error; outDoc["token"] = token; }
        String out; serializeJson(outDoc, out);
        sendJson(req, ok ? 202 : 409, out);
      });
    }
  );

  server.on("/api/ai/test", HTTP_POST,
    [](AsyncWebServerRequest* req){
      if (!requireAuth(req)) return;
      AiTaskArgs args;
      args.token = String((uint32_t)millis()) + "-" + String((uint32_t)esp_random(), HEX);
      args.isTest = true;
      args.req.mode = "general";
      String error, token;
      bool ok = aiJobStart(args, error, token);
      DynamicJsonDocument doc(512);
      doc["ok"] = ok;
      if (ok) { doc["started"] = true; doc["token"] = token; }
      else { doc["error"] = error; doc["token"] = token; }
      String out; serializeJson(doc, out);
      sendJson(req, ok ? 202 : 409, out);
    }
  );

  server.on("/api/ai/status", HTTP_GET,
    [](AsyncWebServerRequest* req){
      if (!requireAuth(req)) return;
      String out;
      if (!aiJobStatusJson(out)) { sendJson(req, 500, "{\"error\":\"ai_status_failed\"}"); return; }
      sendJson(req, 200, out);
    }
  );

  // ---- /api/ai/cancel ----
  // Cooperative cancel: sets the cancelRequested flag. The running AI task
  // checks it on every chunk (~10-50ms) and aborts cleanly, closing the
  // upstream socket itself. We intentionally do NOT call stop() from this
  // handler — that would race with the reader task and risk deadlocks in
  // WiFiClientSecure under load.
  //
  // Returns 202 if a job was running and the flag was set.
  // Returns 409 if no active job exists.
  server.on("/api/ai/cancel", HTTP_POST,
    [](AsyncWebServerRequest* req){
      if (!requireAuth(req)) return;
      aiJobEnsureMutex();
      bool wasBusy = false;
      String token;
      if (xSemaphoreTake(g_aiJobMutex, pdMS_TO_TICKS(200))) {
        wasBusy = g_aiJob.busy && !g_aiJob.done;
        if (wasBusy) {
          g_aiJob.cancelRequested = true;
          token = g_aiJob.token;
        }
        xSemaphoreGive(g_aiJobMutex);
      }
      DynamicJsonDocument doc(256);
      doc["ok"] = wasBusy;
      doc["cancelRequested"] = wasBusy;
      if (wasBusy) doc["token"] = token;
      else doc["error"] = "no_active_job";
      String out; serializeJson(doc, out);
      sendJson(req, wasBusy ? 202 : 409, out);
    }
  );

  // Filesystem listing (debug)
  server.on("/api/fs", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
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

  // Export (json, csv, txt)
  server.on("/api/export", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
    String format = "json";
    if (req->hasParam("format")) format = req->getParam("format")->value();

    String filter = "";
    if (req->hasParam("filter")) filter = req->getParam("filter")->value();
    filter.toLowerCase();
    const bool onlyNeed = (filter == "need" || filter == "shopping" || filter == "needtobuy");

    // For CSV and TXT, we still need a copy (relatively small for filtered sets).
    // For full JSON export, we use stream-export to avoid building the whole string.
    if (format == "json" && !onlyNeed) {
      // ---- STREAM EXPORT: writes directly to the response stream ----
      // No intermediate String, no giant DynamicJsonDocument in RAM.
      AsyncResponseStream* res = req->beginResponseStream("application/json; charset=utf-8");
      res->addHeader("Cache-Control", "no-store");
      res->addHeader("Content-Disposition", "attachment; filename=\"libationlocker-export.json\"");

      if (!InventoryStore::streamExportJson(*res)) {
        // If streaming failed, we can't unsend headers. Send error in body.
        res->print("{\"error\":\"export_failed\"}");
      }
      req->send(res);
      return;
    }

    // For filtered/csv/txt we use streaming response
    // getAll() is still needed for filtering, but we write to the stream
    // line-by-line instead of accumulating one giant String.
    auto items = InventoryStore::getAll();
    if (onlyNeed) {
      std::vector<Item> filtered;
      filtered.reserve(items.size());
      for (auto& it : items) { if (it.needToBuy) filtered.push_back(it); }
      items = std::move(filtered);
    }

    if (format == "txt" || format == "text") {
      AsyncResponseStream* res = req->beginResponseStream("text/plain; charset=utf-8");
      res->addHeader("Cache-Control", "no-store");
      res->addHeader("Content-Disposition",
        String("attachment; filename=\"") +
        (onlyNeed ? "shopping-list.txt" : "libationlocker-items.txt") + "\"");

      if (onlyNeed) {
        res->print("LibationLocker Shopping List (Need=YES)\n");
        res->print("======================================\n\n");
      } else {
        res->print("LibationLocker Export\n");
        res->print("=====================\n\n");
      }

      if (items.empty()) {
        res->print("(none)\n");
      } else {
        for (auto& it : items) {
          res->print("- ");
          if (it.brand.length()) { res->print(it.brand); if (it.name.length()) res->print(" "); }
          res->print(it.name);
          res->print(" ("); res->print(it.sizeMl); res->print(" mL)");
          if (it.type.length()) { res->print(" ["); res->print(it.type); res->print("]"); }
          res->print("  qty:"); res->print(it.qty);
          res->print("  rem:"); res->print(it.remainingPct); res->print("%");
          if (!isnan(it.abv)) { res->print("  abv:"); res->print(it.abv, 1); }
          if (it.tags.size()) {
            res->print("  tags:");
            for (size_t i = 0; i < it.tags.size(); ++i) { if (i) res->print(";"); res->print(it.tags[i]); }
          }
          res->print("\n");
        }
      }
      req->send(res);
      return;
    }

    if (format == "csv") {
      AsyncResponseStream* res = req->beginResponseStream("text/csv; charset=utf-8");
      res->addHeader("Cache-Control", "no-store");
      res->addHeader("Content-Disposition",
        String("attachment; filename=\"") +
        (onlyNeed ? "shopping-list.csv" : "libationlocker-items.csv") + "\"");

      res->print("id,type,brand,name,sizeMl,abv,qty,remainingPct,needToBuy,rating,tags,notes,updatedAt,version\n");
      for (auto& it : items) {
        String abvStr = isnan(it.abv) ? "" : String(it.abv, 1);
        String tags;
        for (size_t i = 0; i < it.tags.size(); ++i) { if (i) tags += ";"; tags += it.tags[i]; }
        res->print(csvEscape(it.id)); res->print(",");
        res->print(csvEscape(it.type)); res->print(",");
        res->print(csvEscape(it.brand)); res->print(",");
        res->print(csvEscape(it.name)); res->print(",");
        res->print(it.sizeMl); res->print(",");
        res->print(csvEscape(abvStr)); res->print(",");
        res->print(it.qty); res->print(",");
        res->print(it.remainingPct); res->print(",");
        res->print(it.needToBuy ? "YES" : "NO"); res->print(",");
        res->print(it.rating); res->print(",");
        res->print(csvEscape(tags)); res->print(",");
        res->print(csvEscape(it.notes)); res->print(",");
        res->print(it.updatedAt); res->print(",");
        res->print(it.version); res->print("\n");
      }
      req->send(res);
      return;
    }

    // Filtered JSON (items only, not full bundle)
    {
      AsyncResponseStream* res = req->beginResponseStream("application/json; charset=utf-8");
      res->addHeader("Cache-Control", "no-store");
      res->print("{\"app\":\"LibationLocker\",\"version\":1,\"filter\":\"need\",\"items\":[");
      for (size_t i = 0; i < items.size(); ++i) {
        if (i) res->print(",");
        // Reuse the per-item serializer from the /api/items endpoint
        StaticJsonDocument<1024> doc;
        const Item& it = items[i];
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
        serializeJson(doc, *res);
      }
      res->print("]}");
      req->send(res);
    }
  });

  // Import (JSON)
  server.on("/api/import", HTTP_POST,
    [](AsyncWebServerRequest* req){},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      if (!requireAuth(req)) return;
      handleBodyCollect_(req, data, len, index, total, [req](const String& body){
        String mode = "merge";
        bool dryRun = false;
        if (req->hasParam("mode")) mode = req->getParam("mode")->value();
        if (req->hasParam("dryrun")) { String v = req->getParam("dryrun")->value(); dryRun = (v == "1" || v == "true" || v == "yes"); }
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

  // Fallback
  server.onNotFound([](AsyncWebServerRequest* req){
    if (!requireAuth(req)) return;
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
