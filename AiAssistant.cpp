#include "AiAssistant.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <vector>
#include <map>
#include <memory>
#include <functional>

#include "InventoryStore.h"
#include "PsramAllocator.h"
#include "AppSerial.h"
#include <esp_task_wdt.h>

// ---------------------------------------------------------------------------
// Provider helpers
// ---------------------------------------------------------------------------
// Provider IDs (string-compared throughout to match what's stored in config):
//   "lmstudio"      — local LM Studio, HTTP, OpenAI Chat Completions shape
//   "openai"        — api.openai.com, HTTPS, Chat Completions shape
//   "anthropic"     — api.anthropic.com, HTTPS, /v1/messages shape
//   "openai_compat" — generic OpenAI-compatible HTTPS host (Groq, Together,
//                     OpenRouter, Mistral, Gemini-OpenAI-compat, etc.)
//
// Anything not in this list is treated as "lmstudio" by the config parser.

static bool providerIsAnthropic(const String& p) { return p == "anthropic"; }
static bool providerIsOpenAI(const String& p)    { return p == "openai"; }
static bool providerIsLmStudio(const String& p)  { return p == "lmstudio" || p.length() == 0; }
static bool providerIsHttps(const String& baseUrl, const String& provider) {
  if (baseUrl.startsWith("https://")) return true;
  // Cloud providers default to HTTPS even if the user typed bare host
  return providerIsAnthropic(provider) || providerIsOpenAI(provider);
}

// Default base URL when the user hasn't set one (used in error messages
// and as a hint; the actual config write path is in the UI/PUT handler).
static String providerDefaultBaseUrl(const String& provider) {
  if (providerIsOpenAI(provider))    return "https://api.openai.com";
  if (providerIsAnthropic(provider)) return "https://api.anthropic.com";
  return ""; // LM Studio + openai_compat: user must supply
}

// Provider-friendly display name (for serial logs + error messages)
static const char* providerDisplayName(const String& provider) {
  if (providerIsOpenAI(provider))    return "OpenAI";
  if (providerIsAnthropic(provider)) return "Anthropic";
  if (provider == "openai_compat")   return "OpenAI-compatible endpoint";
  return "LM Studio";
}

// ---------------------------------------------------------------------------
// Tuning constants — adjusted for N16R8 (16 MB flash, 8 MB PSRAM)
// ---------------------------------------------------------------------------

// ArduinoJson document capacity for parsing LM Studio responses.
// 196 KB in PSRAM is safe; a 4096-token response serialized as JSON rarely
// exceeds 100 KB.
static constexpr size_t AI_DOC_CAPACITY          = 196608;  // was 65536

// NOTE: HTTP timeout for inference is now driven by cfg.aiTimeoutSec
// (configurable from the web UI, default 180s).  No compile-time constant.

// Maximum chars we will pack into the inventory section of the user prompt.
// With the compact pipe-delimited format (~70 chars/item) this allows ~1400
// items before truncation.
static constexpr size_t AI_INVENTORY_CHAR_LIMIT  = 98304;   // was 24000

// Maximum chars we will accept from the model answer before clipping.
static constexpr size_t AI_RESPONSE_CHAR_LIMIT   = 49152;   // was 24000

// Max bytes we will read from the HTTP response body (protects against
// runaway streams).
static constexpr size_t AI_RESPONSE_READ_LIMIT   = 262144;

// Model-list cache TTL.
static constexpr uint32_t AI_MODEL_CACHE_TTL_MS  = 30000;

// ---------------------------------------------------------------------------
// Module-level caches
// ---------------------------------------------------------------------------
static std::vector<AiModelInfo> s_modelCache;
static uint32_t s_modelCacheMs     = 0;

// Inventory cache — stored in a PsramString to avoid internal-heap pressure.
static PsramString s_inventoryCache;
static uint32_t    s_inventorySig   = 0;
static size_t      s_inventoryItems = 0;
static size_t      s_inventoryTotal = 0;
static bool        s_inventoryTrunc = false;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// URL host/port parser — extracts host and port from http://host:port/path
// ---------------------------------------------------------------------------
static bool parseHostPort(const String& url, String& host, uint16_t& port) {
  String tmp = url;
  if (tmp.startsWith("http://"))  tmp = tmp.substring(7);
  if (tmp.startsWith("https://")) tmp = tmp.substring(8);
  int slashIdx = tmp.indexOf('/');
  String hostPort = (slashIdx > 0) ? tmp.substring(0, slashIdx) : tmp;
  int colonIdx = hostPort.indexOf(':');
  if (colonIdx > 0) {
    host = hostPort.substring(0, colonIdx);
    port = hostPort.substring(colonIdx + 1).toInt();
  } else {
    host = hostPort;
    port = url.startsWith("https") ? 443 : 80;
  }
  return host.length() > 0;
}

// ---------------------------------------------------------------------------
// Transport factory — returns either WiFiClient (plain TCP) or
// WiFiClientSecure (TLS). Caller owns the client and is responsible for
// connect()/stop().
//
// Why a custom deleter: WiFiClient's destructor is not virtual in older
// Arduino-ESP32 cores. Casting WiFiClientSecure* to WiFiClient* and then
// deleting through the base would leak the mbedTLS session. The deleter
// remembers the original type and deletes through the correct pointer.
//
// TLS notes:
//   - setInsecure() skips cert verification. Acceptable here because the
//     real auth is the API key (Authorization: Bearer / x-api-key).
//   - For pinned cert validation: replace setInsecure() with setCACert()
//     and manage the cert in flash.
// ---------------------------------------------------------------------------
struct WiFiClientDeleter {
  bool isSecure = false;
  void operator()(WiFiClient* p) const {
    if (!p) return;
    if (isSecure) {
      delete static_cast<WiFiClientSecure*>(p);
    } else {
      delete p;
    }
  }
};

using WiFiClientPtr = std::unique_ptr<WiFiClient, WiFiClientDeleter>;

static WiFiClientPtr makeClient(bool useTls, bool tlsInsecure) {
  if (useTls) {
    auto* secure = new WiFiClientSecure();
    if (tlsInsecure) {
      secure->setInsecure();
    }
    return WiFiClientPtr(secure, WiFiClientDeleter{true});
  }
  return WiFiClientPtr(new WiFiClient(), WiFiClientDeleter{false});
}

// ---------------------------------------------------------------------------
// httpGetJson — provider-aware GET with correct auth header per provider
// ---------------------------------------------------------------------------
// For OpenAI / LM Studio / openai_compat: Authorization: Bearer <key>
// For Anthropic: x-api-key + anthropic-version (required by the API)
// ---------------------------------------------------------------------------

static bool httpGetJson(const String& endpoint, const String& apiKey,
                        const String& provider, bool tlsInsecure,
                        String& bodyOut, int& codeOut, String& errorOut,
                        uint32_t connectTimeoutMs = 4000,
                        uint32_t readTimeoutMs = 4000) {
  const bool useTls = endpoint.startsWith("https://");
  auto client = makeClient(useTls, tlsInsecure);

  HTTPClient http;
  http.setReuse(false);

  // Cap to avoid uint16_t overflow in HTTPClient::setTimeout()
  http.setTimeout(min(readTimeoutMs, (uint32_t)60000));
  http.setConnectTimeout(min(connectTimeoutMs, (uint32_t)60000));

  APP_SERIAL.printf("[AI] GET %s\n", endpoint.c_str());
  if (!http.begin(*client, endpoint)) {
    errorOut = String("Failed to initialize HTTP client for ") + endpoint;
    return false;
  }
  http.addHeader("Accept", "application/json");

  if (apiKey.length()) {
    if (providerIsAnthropic(provider)) {
      http.addHeader("x-api-key", apiKey);
      http.addHeader("anthropic-version", "2023-06-01");
    } else {
      http.addHeader("Authorization", String("Bearer ") + apiKey);
    }
  }

  codeOut = http.GET();
  if (codeOut <= 0) {
    errorOut = String("HTTP GET failed for ") + endpoint + ": " + http.errorToString(codeOut);
    http.end();
    return false;
  }
  bodyOut = http.getString();
  http.end();
  APP_SERIAL.printf("[AI] GET %s -> HTTP %d, %u bytes\n",
                    endpoint.c_str(), codeOut, (unsigned)bodyOut.length());
  return true;
}

// ---------------------------------------------------------------------------
// aiPreflight — gates all AI calls
// ---------------------------------------------------------------------------
// Returns false with a human-readable error if any prerequisite is missing.
// Cloud providers (OpenAI, Anthropic) additionally require an API key.
// ---------------------------------------------------------------------------
static bool aiPreflight(const DropdownConfig& cfg, String& baseUrl,
                         String& error, bool requireModel = true) {
  baseUrl = AiAssistant::normalizeBaseUrl(cfg.aiBaseUrl);
  const char* provName = providerDisplayName(cfg.aiProvider);

  if (!cfg.aiEnabled) { error = "AI is disabled in Config."; return false; }

  // For cloud providers, fall back to the canonical base URL if user left it blank.
  if (!baseUrl.length()) {
    String def = providerDefaultBaseUrl(cfg.aiProvider);
    if (def.length()) {
      baseUrl = def;
    } else {
      error = String(provName) + " base URL is blank.";
      return false;
    }
  }

  if (requireModel && !cfg.aiModel.length()) {
    error = String(provName) + " model is blank.";
    return false;
  }

  // Cloud providers require an API key
  if ((providerIsOpenAI(cfg.aiProvider) || providerIsAnthropic(cfg.aiProvider))
      && !cfg.aiApiKey.length()) {
    error = String(provName) + " requires an API key (set ai.apiKey in config).";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    error = String("STA Wi-Fi is not connected. ") + provName +
            " is reachable only over STA/LAN.";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Public utility
// ---------------------------------------------------------------------------

String AiAssistant::normalizeBaseUrl(const String& raw) {
  String url = raw;
  url.trim();
  if (!url.length()) return String();
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    url = String("http://") + url;
  }
  while (url.endsWith("/")) url.remove(url.length() - 1);
  return url;
}

// ---------------------------------------------------------------------------
// JSON escaping — two variants
// ---------------------------------------------------------------------------

String AiAssistant::jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 32);
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b";  break;
      case '\f': out += "\\f";  break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(uint8_t)c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// Append JSON-escaped version of (s, sLen) directly into a PsramString.
// Avoids creating an intermediate Arduino String for the escaped payload.
void AiAssistant::jsonEscapeInto(PsramString& out, const char* s, size_t sLen) {
  for (size_t i = 0; i < sLen; ++i) {
    const char c = s[i];
    switch (c) {
      case '"':  out.append("\\\"", 2); break;
      case '\\': out.append("\\\\", 2); break;
      case '\b': out.append("\\b", 2);  break;
      case '\f': out.append("\\f", 2);  break;
      case '\n': out.append("\\n", 2);  break;
      case '\r': out.append("\\r", 2);  break;
      case '\t': out.append("\\t", 2);  break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(uint8_t)c);
          out.append(buf, 6);
        } else {
          out.append(c);
        }
    }
  }
}

// ---------------------------------------------------------------------------
// Inventory signature (FNV-1a over mutable item fields)
// ---------------------------------------------------------------------------

static uint32_t fnv1aUpdate(uint32_t hash, const String& s) {
  const uint8_t* p = (const uint8_t*)s.c_str();
  for (size_t i = 0; i < s.length(); ++i) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t AiAssistant::inventorySignature() {
  uint32_t hash = 2166136261u;
  InventoryStore::forEach([&](const Item& it) -> bool {
    hash = fnv1aUpdate(hash, it.id);
    hash ^= (uint32_t)it.version;      hash *= 16777619u;
    hash ^= (uint32_t)it.qty;          hash *= 16777619u;
    hash ^= (uint32_t)it.remainingPct; hash *= 16777619u;
    hash ^= (uint32_t)(it.needToBuy ? 1 : 0); hash *= 16777619u;
    return true;
  });
  return hash;
}

// ---------------------------------------------------------------------------
// Normalized hints (unchanged logic, kept compact)
// ---------------------------------------------------------------------------

String AiAssistant::normalizedHintsForItem(const String& type, const String& brand,
                                            const String& name,
                                            const std::vector<String>& tags) {
  String hay = type + " " + brand + " " + name;
  for (const auto& t : tags) { hay += " "; hay += t; }
  hay.toLowerCase();

  String hints;
  auto add = [&](const char* h) { if (hints.length()) hints += '|'; hints += h; };

  if (hay.indexOf("bourbon")     >= 0) add("bourbon");
  if (hay.indexOf("rye")         >= 0) add("rye");
  if (hay.indexOf("scotch")      >= 0 || hay.indexOf("single malt") >= 0) add("scotch");
  if (hay.indexOf("whiskey")     >= 0 || hay.indexOf("whisky") >= 0) add("whiskey");
  if (hay.indexOf("gin")         >= 0) add("gin");
  if (hay.indexOf("vodka")       >= 0) add("vodka");
  if (hay.indexOf("rum")         >= 0) add("rum");
  if (hay.indexOf("tequila")     >= 0) add("tequila");
  if (hay.indexOf("mezcal")      >= 0) add("mezcal");
  if (hay.indexOf("vermouth")    >= 0) add("vermouth");
  if (hay.indexOf("amaro")       >= 0 || hay.indexOf("aperitif") >= 0) add("amaro");
  if (hay.indexOf("liqueur")     >= 0 || hay.indexOf("triple sec") >= 0 ||
      hay.indexOf("curaçao")     >= 0 || hay.indexOf("curacao") >= 0) add("liqueur");
  if (hay.indexOf("bitters")     >= 0) add("bitters");
  if (hay.indexOf("syrup")       >= 0 || hay.indexOf("simple") >= 0) add("sweetener");
  if (hay.indexOf("lime")        >= 0 || hay.indexOf("lemon") >= 0 ||
      hay.indexOf("citrus")      >= 0) add("citrus");
  if (hay.indexOf("orange")      >= 0) add("orange");
  if (hay.indexOf("brandy")      >= 0 || hay.indexOf("cognac") >= 0) add("brandy");
  if (hay.indexOf("absinthe")    >= 0) add("absinthe");
  if (hay.indexOf("champagne")   >= 0 || hay.indexOf("prosecco") >= 0 ||
      hay.indexOf("sparkling")   >= 0) add("sparkling");
  if (hay.indexOf("wine")        >= 0) add("wine");
  return hints;
}

// ---------------------------------------------------------------------------
// Mode instruction text
// ---------------------------------------------------------------------------

String AiAssistant::modeInstruction(const String& modeRaw) {
  // This is now only a fallback — the primary path looks up modes from config.
  // Kept for backward compatibility if config has no modes.
  String mode = modeRaw;
  mode.toLowerCase();
  if (mode == "can_make_now")
    return "List cocktails/drinks that can be made immediately from current inventory only. Short list with reasoning.";
  if (mode == "missing_ingredients")
    return "Focus on drinks that are nearly possible. List what is missing bottle-by-bottle.";
  if (mode == "recommend_purchases")
    return "Recommend highest-value purchases that unlock the most cocktails based on current inventory.";
  if (mode == "shopping_list")
    return "Build a practical shopping list grouped into: bottles, mixers, citrus, syrups, optional extras.";
  return "Answer the question freely. You may provide recipes, cocktail knowledge, spirit education, purchase advice, or anything else the user asks about.";
}

// Look up an AiMode from config by id.  Returns nullptr if not found.
static const AiMode* findMode(const std::vector<AiMode>& modes, const String& id) {
  for (const auto& m : modes) {
    if (m.id == id) return &m;
  }
  return nullptr;
}


// ---------------------------------------------------------------------------
// buildInventoryContext — compact pipe-delimited format + grouped summary
//
// Format (one line per item, pipe-separated):
//   type|brand|name|sizeMl|abv|qty|remaining%|needToBuy|hints|tags
//
// A grouped summary block precedes the item lines so the LLM has a
// structural overview even if individual items are later truncated.
// ---------------------------------------------------------------------------

bool AiAssistant::buildInventoryContext(PsramString& out, bool& truncated,
                                        bool& cacheHit,
                                        size_t& itemsSerialized,
                                        size_t& itemsTotal) {
  const uint32_t sig = inventorySignature();
  itemsTotal = InventoryStore::count();

  // ---- cache hit? ----
  if (s_inventorySig != 0 && sig == s_inventorySig && !s_inventoryCache.empty()) {
    // Copy cached PsramString into out
    out.reserve(s_inventoryCache.length() + 1);
    out.append(s_inventoryCache.c_str(), s_inventoryCache.length());
    truncated       = s_inventoryTrunc;
    cacheHit        = true;
    itemsSerialized = s_inventoryItems;
    itemsTotal      = s_inventoryTotal;
    return true;
  }

  cacheHit  = false;
  truncated = false;
  itemsSerialized = 0;

  // Pre-allocate in PSRAM
  out.reserve(AI_INVENTORY_CHAR_LIMIT + 1024);

  // ---- Phase 1: Build per-type summary ----
  // Collect type -> (count, totalMl, needToBuyCount) under the inventory lock.
  struct TypeSummary { size_t count = 0; size_t totalMl = 0; size_t needBuy = 0; };
  std::map<String, TypeSummary> typeSummaries;
  size_t totalQty = 0;
  size_t needToBuyCount = 0;

  InventoryStore::forEach([&](const Item& it) -> bool {
    auto& ts = typeSummaries[it.type];
    ts.count++;
    ts.totalMl += (size_t)((it.qty > 0 ? it.qty : 0) * it.sizeMl * it.remainingPct / 100);
    if (it.needToBuy) { ts.needBuy++; needToBuyCount++; }
    totalQty += (size_t)((it.qty > 0) ? it.qty : 0);
    return true;
  });

  out.append("LibationLocker inventory snapshot. Compact format: type|brand|name|sizeMl|abv|qty|remain%|needBuy|hints|tags\n");
  out.appendf("TOTALS: %u items, %u bottles, %u flagged need-to-buy\n",
              (unsigned)itemsTotal, (unsigned)totalQty, (unsigned)needToBuyCount);

  // Per-type summary line
  out.append("BY TYPE: ");
  bool firstType = true;
  for (const auto& kv : typeSummaries) {
    if (!firstType) out.append("; ");
    firstType = false;
    out.appendf("%s(%u, ~%uml", kv.first.c_str(), (unsigned)kv.second.count,
                (unsigned)kv.second.totalMl);
    if (kv.second.needBuy > 0) out.appendf(", %u need-buy", (unsigned)kv.second.needBuy);
    out.append(")");
  }
  out.append("\n---\n");

  // ---- Phase 2: Item lines in compact format ----
  InventoryStore::forEach([&](const Item& it) -> bool {
    // Build one line into a small stack buffer, then check limit
    char line[384];
    int pos = 0;

    // type|brand|name|sizeMl|abv|qty|remain%|needBuy
    pos += snprintf(line + pos, sizeof(line) - pos, "%s|%s|%s|%d|",
                    it.type.c_str(), it.brand.c_str(), it.name.c_str(), it.sizeMl);
    if (!isnan(it.abv))
      pos += snprintf(line + pos, sizeof(line) - pos, "%.1f", it.abv);
    pos += snprintf(line + pos, sizeof(line) - pos, "|%d|%d|%s",
                    it.qty, it.remainingPct, it.needToBuy ? "Y" : "N");

    // hints
    String hints = normalizedHintsForItem(it.type, it.brand, it.name, it.tags);
    pos += snprintf(line + pos, sizeof(line) - pos, "|%s", hints.c_str());

    // tags (pipe-joined already)
    if (!it.tags.empty()) {
      pos += snprintf(line + pos, sizeof(line) - pos, "|");
      for (size_t i = 0; i < it.tags.size(); ++i) {
        if (i) line[pos++] = ',';
        int remain = (int)sizeof(line) - pos - 1;
        if (remain > 0)
          pos += snprintf(line + pos, remain, "%s", it.tags[i].c_str());
      }
    }

    // Ensure newline
    if (pos < (int)sizeof(line) - 1) line[pos++] = '\n';
    line[pos] = '\0';

    // Check char limit before appending
    if (out.length() + (size_t)pos > AI_INVENTORY_CHAR_LIMIT) {
      truncated = true;
      return false; // stop iteration
    }
    out.append(line, (size_t)pos);
    itemsSerialized++;
    return true;
  });

  if (truncated) {
    out.appendf("\n[Inventory truncated: showed %u of %u items. "
                "Answer using visible subset; say if more data needed.]\n",
                (unsigned)itemsSerialized, (unsigned)itemsTotal);
  }

  // ---- Update cache ----
  s_inventorySig   = sig;
  s_inventoryTrunc = truncated;
  s_inventoryItems = itemsSerialized;
  s_inventoryTotal = itemsTotal;

  // Deep-copy into persistent PSRAM cache
  s_inventoryCache.clear();
  s_inventoryCache.reserve(out.length() + 1);
  s_inventoryCache.append(out.c_str(), out.length());

  APP_SERIAL.printf("[AI] Inventory context built: %u/%u items, %u chars, truncated=%d\n",
                    (unsigned)itemsSerialized, (unsigned)itemsTotal,
                    (unsigned)out.length(), truncated ? 1 : 0);
  return true;
}

// ---------------------------------------------------------------------------
// readResponseBody — read HTTP response into a PSRAM buffer
// ---------------------------------------------------------------------------

static bool readResponseBody(HTTPClient& http, PsramString& out, size_t maxBytes, uint32_t timeoutMs,
                              std::function<bool()> shouldCancel = nullptr) {
  int contentLen = http.getSize();  // -1 if chunked
  size_t estimate = (contentLen > 0) ? (size_t)contentLen : 32768;
  if (estimate > maxBytes) estimate = maxBytes;
  out.reserve(estimate + 1);

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) return false;

  uint8_t buf[2048];
  unsigned long deadline = millis() + timeoutMs;
  uint32_t cancelCheckMs = 0;

  while (stream->connected() || stream->available()) {
    if (millis() > deadline) {
      APP_SERIAL.println("[AI] Response read timed out");
      break;
    }
    // Cooperative cancel — checked at most every 100ms to avoid lock churn
    if (shouldCancel && (millis() - cancelCheckMs > 100)) {
      cancelCheckMs = millis();
      if (shouldCancel()) {
        APP_SERIAL.println("[AI] Buffered read cancelled by caller");
        stream->stop();
        return false;
      }
    }
    int avail = stream->available();
    if (avail <= 0) { delay(5); continue; }
    int toRead = (avail > (int)sizeof(buf)) ? (int)sizeof(buf) : avail;
    if (out.length() + (size_t)toRead > maxBytes) {
      toRead = (int)(maxBytes - out.length());
      if (toRead <= 0) break;
    }
    int got = stream->readBytes(buf, toRead);
    if (got <= 0) break;
    out.append((const char*)buf, (size_t)got);
    if (out.length() >= maxBytes) break;
    vTaskDelay(1);  // yield 1 tick so idle task can feed the WDT
  }
  return out.length() > 0;
}

// ---------------------------------------------------------------------------
// flattenContent — extract text from various LM Studio response shapes
// ---------------------------------------------------------------------------

String AiAssistant::flattenContent(const JsonVariantConst& content) {
  if (content.is<const char*>()) return String(content.as<const char*>());
  if (content.is<String>()) return content.as<String>();
  if (content.is<JsonArrayConst>()) {
    String out;
    for (JsonVariantConst part : content.as<JsonArrayConst>()) {
      if (!part.is<JsonObjectConst>()) continue;
      JsonObjectConst obj = part.as<JsonObjectConst>();
      String type = obj["type"] | "";
      if (type == "text" || type == "output_text") {
        String txt = obj["text"] | "";
        if (txt.length()) {
          if (out.length()) out += "\n";
          out += txt;
        }
      }
    }
    return out;
  }
  return String();
}

// ---------------------------------------------------------------------------
// Filter chat-incompatible OpenAI models so the UI dropdown only shows
// models the chat-completions endpoint will actually accept.
// ---------------------------------------------------------------------------
static bool isOpenAiChatModelId(const String& id) {
  String lo = id; lo.toLowerCase();
  // Reject embeddings, TTS, transcription, image, moderation, realtime
  if (lo.indexOf("embedding")     >= 0) return false;
  if (lo.indexOf("whisper")       >= 0) return false;
  if (lo.indexOf("tts-")          >= 0) return false;
  if (lo.startsWith("dall-e"))          return false;
  if (lo.indexOf("moderation")    >= 0) return false;
  if (lo.indexOf("transcribe")    >= 0) return false;
  if (lo.indexOf("realtime")      >= 0) return false;
  if (lo.indexOf("audio")         >= 0 && lo.indexOf("gpt") < 0) return false;
  // Heuristic accept: gpt-*, o1, o3, chatgpt-*
  if (lo.startsWith("gpt-"))       return true;
  if (lo.startsWith("chatgpt-"))   return true;
  if (lo == "o1" || lo.startsWith("o1-")) return true;
  if (lo == "o3" || lo.startsWith("o3-")) return true;
  if (lo == "o4" || lo.startsWith("o4-")) return true;
  // Default deny — OpenAI introduces non-chat models often. Better to
  // hide than to confuse the user with a model that 400s on chat.
  return false;
}

// ---------------------------------------------------------------------------
// listModels — provider-aware
// ---------------------------------------------------------------------------
// LM Studio / OpenAI / openai_compat: GET /v1/models -> { data: [{id, ...}] }
// Anthropic: GET /v1/models -> { data: [{id, type, display_name, ...}] }
// (Anthropic added /v1/models in 2024; same shape, just different fields.)
// ---------------------------------------------------------------------------

bool AiAssistant::listModels(std::vector<AiModelInfo>& models, String& error,
                              bool forceRefresh) {
  models.clear();
  DropdownConfig cfg = InventoryStore::getConfig();
  String baseUrl;
  if (!aiPreflight(cfg, baseUrl, error, false)) {
    APP_SERIAL.printf("[AI] Model list preflight failed: %s\n", error.c_str());
    return false;
  }

  const uint32_t nowMs = millis();
  if (!forceRefresh && !s_modelCache.empty() &&
      (uint32_t)(nowMs - s_modelCacheMs) < AI_MODEL_CACHE_TTL_MS) {
    models = s_modelCache;
    APP_SERIAL.printf("[AI] Using cached model list (%u models)\n", (unsigned)models.size());
    return true;
  }

  // Endpoint suffixes to try, per provider.
  std::vector<const char*> suffixes;
  if (providerIsAnthropic(cfg.aiProvider)) {
    suffixes = {"/v1/models"};
  } else {
    // LM Studio + OpenAI + openai_compat
    suffixes = {"/v1/models", "/api/v1/models", "/api/models"};
  }

  String lastBody;
  int lastCode = 0;
  String lastEndpoint;

  for (const char* suffix : suffixes) {
    esp_task_wdt_reset();
    String endpoint = baseUrl + suffix;
    String body;
    int code = 0;
    String reqErr;
    if (!httpGetJson(endpoint, cfg.aiApiKey, cfg.aiProvider, cfg.aiTlsInsecure,
                     body, code, reqErr)) {
      error = reqErr; lastEndpoint = endpoint; continue;
    }
    lastBody = body; lastCode = code; lastEndpoint = endpoint;
    if (code < 200 || code >= 300) {
      error = String("AI endpoint returned HTTP ") + code + " from " + endpoint;
      continue;
    }

    PsramJsonDocument doc(AI_DOC_CAPACITY);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      error = String("Failed parsing models response from ") + endpoint + ": " + err.c_str();
      continue;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    JsonArrayConst rootArr = doc.as<JsonArrayConst>();
    JsonArrayConst arr = !data.isNull() ? data : rootArr;
    if (arr.isNull()) {
      error = String("Models response from ") + endpoint + " has no data array.";
      continue;
    }

    for (JsonVariantConst v : arr) {
      JsonObjectConst obj = v.as<JsonObjectConst>();
      if (obj.isNull()) continue;
      String id = obj["id"] | obj["name"] | "";
      if (!id.length()) continue;

      // OpenAI: hide non-chat models (embeddings, TTS, etc.)
      if (providerIsOpenAI(cfg.aiProvider) && !isOpenAiChatModelId(id)) {
        continue;
      }

      bool exists = false;
      for (const auto& m : models) { if (m.id == id) { exists = true; break; } }
      if (exists) continue;

      AiModelInfo info;
      info.id = id;
      info.objectType = obj["object"] | "model";
      String ownedBy = obj["owned_by"] | "";
      ownedBy.toLowerCase();
      info.ownedByLmStudio = ownedBy.indexOf("lm_studio") >= 0 || ownedBy.indexOf("lmstudio") >= 0;
      models.push_back(info);
    }

    if (!models.empty()) {
      s_modelCache = models;
      s_modelCacheMs = nowMs;
      APP_SERIAL.printf("[AI] Parsed %u model(s) from %s (provider=%s)\n",
                        (unsigned)models.size(), endpoint.c_str(),
                        providerDisplayName(cfg.aiProvider));
      return true;
    }
    error = String("AI endpoint returned no models from ") + endpoint;
  }

  if (lastCode > 0) {
    String snippet = lastBody;
    if (snippet.length() > 300) snippet = snippet.substring(0, 300) + "...";
    error += String("; last endpoint=") + lastEndpoint + "; http=" + lastCode + "; body=" + snippet;
  } else if (lastEndpoint.length()) {
    error += String("; last endpoint=") + lastEndpoint;
  }
  APP_SERIAL.printf("[AI] Model list failed: %s\n", error.c_str());
  return false;
}

// ---------------------------------------------------------------------------
// validateConfiguredModel
// ---------------------------------------------------------------------------

bool AiAssistant::validateConfiguredModel(String& error) {
  DropdownConfig cfg = InventoryStore::getConfig();
  const char* provName = providerDisplayName(cfg.aiProvider);
  if (!cfg.aiModel.length()) { error = String(provName) + " model is blank."; return false; }
  std::vector<AiModelInfo> models;
  if (!listModels(models, error, false)) return false;
  for (const auto& m : models) {
    if (m.id == cfg.aiModel) return true;
  }
  error = String("Configured model not found in ") + provName + ": " + cfg.aiModel;
  return false;
}

// ---------------------------------------------------------------------------
// buildChatPayload — shared payload builder used by both transports
// ---------------------------------------------------------------------------
// Writes a complete OpenAI / LM Studio / Anthropic chat-completions JSON
// payload into `payload`. If `streaming` is true, injects "stream":true and
// (for OpenAI/LM Studio) "stream_options":{"include_usage":true}.
// ---------------------------------------------------------------------------
static void buildChatPayload(PsramString& payload,
                              const DropdownConfig& cfg,
                              const char* systemPrompt, size_t systemPromptLen,
                              const char* userPrompt,   size_t userPromptLen,
                              bool streaming) {
  auto needsMaxCompletionTokens = [](const String& model) -> bool {
    String lo = model; lo.toLowerCase();
    if (lo.startsWith("o1") || lo.startsWith("o3") || lo.startsWith("o4")) return true;
    if (lo.startsWith("gpt-5")) return true;
    return false;
  };

  payload.reserve(systemPromptLen + userPromptLen + 1024);

  if (providerIsAnthropic(cfg.aiProvider)) {
    payload.append("{\"model\":\"");
    AiAssistant::jsonEscapeInto(payload, cfg.aiModel.c_str(), cfg.aiModel.length());
    payload.append("\",\"max_tokens\":");
    payload.appendf("%d", cfg.aiMaxTokens);
    payload.append(",\"temperature\":");
    payload.appendf("%.2f", cfg.aiTemperature);
    if (streaming) payload.append(",\"stream\":true");
    payload.append(",\"system\":\"");
    AiAssistant::jsonEscapeInto(payload, systemPrompt, systemPromptLen);
    payload.append("\",\"messages\":[{\"role\":\"user\",\"content\":\"");
    AiAssistant::jsonEscapeInto(payload, userPrompt, userPromptLen);
    payload.append("\"}]}");
  } else {
    const bool useMaxCompletion = providerIsOpenAI(cfg.aiProvider) &&
                                  needsMaxCompletionTokens(cfg.aiModel);

    payload.append("{\"model\":\"");
    AiAssistant::jsonEscapeInto(payload, cfg.aiModel.c_str(), cfg.aiModel.length());

    if (!useMaxCompletion) {
      payload.append("\",\"temperature\":");
      payload.appendf("%.2f", cfg.aiTemperature);
      payload.append(",\"max_tokens\":");
    } else {
      payload.append("\",\"max_completion_tokens\":");
    }
    payload.appendf("%d", cfg.aiMaxTokens);

    if (streaming) {
      payload.append(",\"stream\":true");
      // include_usage gives a final SSE chunk with token counts. Most
      // openai_compat servers ignore unknown options. LM Studio accepts it.
      payload.append(",\"stream_options\":{\"include_usage\":true}");
    }

    payload.append(",\"messages\":[{\"role\":\"system\",\"content\":\"");
    AiAssistant::jsonEscapeInto(payload, systemPrompt, systemPromptLen);
    payload.append("\"},{\"role\":\"user\",\"content\":\"");
    AiAssistant::jsonEscapeInto(payload, userPrompt, userPromptLen);
    payload.append("\"}]}");
  }
}

// ---------------------------------------------------------------------------
// extractSseDelta — parse a single SSE data line's JSON and append text
// ---------------------------------------------------------------------------
// Handles three shapes:
//   OpenAI / LM Studio:  data: {"choices":[{"delta":{"content":"..."}, ...}], ...}
//   OpenAI reasoning :   delta.reasoning_content (Qwen / DeepSeek-R1 in LM Studio)
//   Anthropic        :   data: {"type":"content_block_delta",
//                                "delta":{"type":"text_delta","text":"..."}}
//
// Returns true if any text was extracted into outDelta. Also extracts
// finish_reason / stop_reason into finishOut when present.
// ---------------------------------------------------------------------------
static bool extractSseDelta(const char* json, size_t jsonLen,
                             bool isAnthropic,
                             String& outDelta, String& finishOut,
                             bool& usedReasoning) {
  // Per-chunk JSON is small — 4 KB is plenty (tokens are short)
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, json, jsonLen);
  if (err) return false;

  if (isAnthropic) {
    String type = doc["type"] | "";
    if (type == "content_block_delta") {
      String dtype = doc["delta"]["type"] | "";
      if (dtype == "text_delta") {
        String t = doc["delta"]["text"] | "";
        if (t.length()) { outDelta = t; return true; }
      }
    } else if (type == "message_delta") {
      String stop = doc["delta"]["stop_reason"] | "";
      if (stop.length()) finishOut = stop;
    } else if (type == "message_stop") {
      // terminal marker; nothing to extract
    }
    return false;
  }

  // OpenAI / LM Studio
  JsonArrayConst choices = doc["choices"].as<JsonArrayConst>();
  if (!choices.isNull() && choices.size() > 0) {
    JsonObjectConst delta = choices[0]["delta"].as<JsonObjectConst>();
    if (!delta.isNull()) {
      String t = delta["content"] | "";
      if (t.length()) { outDelta = t; return true; }
      // Qwen 3.x / DeepSeek-R1 emit reasoning_content in deltas
      String r = delta["reasoning_content"] | "";
      if (r.length()) { outDelta = r; usedReasoning = true; return true; }
    }
    String fin = choices[0]["finish_reason"] | "";
    if (fin.length()) finishOut = fin;
  }
  return false;
}

// ---------------------------------------------------------------------------
// askStreamed — SSE streaming POST, the recommended path
// ---------------------------------------------------------------------------
//
// Sends "stream":true and reads the SSE response line-by-line. Each chunk
// arrives within ~10-50ms of the previous one, so the TCP socket is never
// idle and the read timeout never trips on long generations.
//
// Cancellation is cooperative: cb.shouldCancel() is polled between chunks
// (and during slow reads). On cancel, the socket is closed and resp.cancelled
// is set; resp.answer holds whatever was accumulated.
// ---------------------------------------------------------------------------
bool AiAssistant::askStreamed(const AiAskRequest& req, AiAskResponse& resp,
                               const AiStreamCallbacks& cb) {
  DropdownConfig cfg = InventoryStore::getConfig();
  String baseUrl;
  if (!aiPreflight(cfg, baseUrl, resp.error, true)) return false;

  String validationError;
  resp.modelValidated = validateConfiguredModel(validationError);
  if (!resp.modelValidated) { resp.error = validationError; return false; }

  // ---- Build inventory context ----
  PsramString inventoryCtx;
  bool truncated = false, cacheHit = false;
  size_t itemsSerialized = 0, itemsTotal = 0;
  if (req.includeInventory) {
    buildInventoryContext(inventoryCtx, truncated, cacheHit, itemsSerialized, itemsTotal);
    resp.inventoryIncluded   = true;
    resp.inventoryChars      = inventoryCtx.length();
    resp.inventoryTruncated  = truncated;
    resp.inventoryCacheHit   = cacheHit;
    resp.inventoryItemCount  = itemsSerialized;
    resp.inventoryTotalItems = itemsTotal;
  }

  // ---- Resolve mode ----
  String mode = req.mode;
  mode.trim(); mode.toLowerCase();
  if (!mode.length()) mode = "general";
  resp.modeUsed = mode;

  const std::vector<AiMode>& modes = cfg.aiModes.empty() ? defaultAiModes() : cfg.aiModes;
  const AiMode* am = findMode(modes, mode);
  if (!am && !modes.empty()) am = &modes[0];
  const bool restricted = am ? am->restrictToInventory : false;

  // ---- System prompt ----
  String systemPrompt;
  if (am && am->systemPrompt.length()) {
    systemPrompt = am->systemPrompt;
  } else {
    systemPrompt = cfg.aiSystemPrompt;
    systemPrompt.trim();
  }
  if (!systemPrompt.length()) {
    if (restricted) {
      systemPrompt =
        "You are LibationLocker AI, a bartender assistant. "
        "The user's inventory is provided in compact pipe-delimited format: "
        "type|brand|name|sizeMl|abv|qty|remain%|needBuy|hints|tags. "
        "A BY TYPE summary precedes the item lines. "
        "Answer using ONLY the provided inventory. "
        "Be concise and structured. Use bullets. Clearly label missing ingredients.";
    } else {
      systemPrompt =
        "You are LibationLocker AI, a knowledgeable bartender and cocktail assistant. "
        "Answer any question the user asks. You may provide drink recipes, general cocktail knowledge, "
        "food pairing advice, spirit education, or anything else. "
        "If the user's inventory is included, reference it when relevant but do not limit yourself to it. "
        "Be concise and structured. Use bullets where helpful.";
    }
  }
  if (cfg.aiDisableThinking && providerIsLmStudio(cfg.aiProvider)) {
    systemPrompt += " /no_think";
  }

  // ---- User prompt ----
  PsramString userPrompt;
  userPrompt.reserve(req.question.length() + inventoryCtx.length() + 1024);
  userPrompt.append("Task mode: ");
  userPrompt.append(am ? am->label.c_str() : mode.c_str());
  userPrompt.append("\nInstruction: ");
  {
    String inst = (am && am->instruction.length()) ? am->instruction : modeInstruction(mode);
    userPrompt.append(inst.c_str(), inst.length());
  }
  userPrompt.append("\n\nUser question:\n");
  userPrompt.append(req.question.c_str(), req.question.length());
  userPrompt.append("\n\n");

  if (req.includeInventory) {
    userPrompt.append(inventoryCtx.c_str(), inventoryCtx.length());
    if (restricted) {
      userPrompt.append("\nUse only this inventory snapshot. Clearly label missing ingredients.\n");
    } else {
      userPrompt.append("\nThe inventory above is provided for reference. Use it when relevant but you are not restricted to it.\n");
    }
    if (truncated) {
      userPrompt.append("NOTE: Inventory was truncated. State that limitation if it materially affects the answer.\n");
    }
  } else {
    userPrompt.append("No inventory snapshot included with this request.\n");
  }

  // ---- Build payload ----
  PsramString payload;
  buildChatPayload(payload, cfg, systemPrompt.c_str(), systemPrompt.length(),
                   userPrompt.c_str(), userPrompt.length(), /*streaming=*/true);

  APP_SERIAL.printf("[AI/STREAM] Provider=%s, payload=%u chars, inventory=%u chars (%u/%u items)\n",
                    providerDisplayName(cfg.aiProvider),
                    (unsigned)payload.length(), (unsigned)inventoryCtx.length(),
                    (unsigned)itemsSerialized, (unsigned)itemsTotal);

  inventoryCtx.clear();
  userPrompt.clear();

  // ---- Transport ----
  const uint32_t httpTimeoutMs = (uint32_t)cfg.aiTimeoutSec * 1000;
  const String endpoint = providerIsAnthropic(cfg.aiProvider)
                          ? (baseUrl + "/v1/messages")
                          : (baseUrl + "/v1/chat/completions");

  const bool useTls = providerIsHttps(baseUrl, cfg.aiProvider);
  auto wifiClient = makeClient(useTls, cfg.aiTlsInsecure);

  String aiHost;
  uint16_t aiPort = useTls ? 443 : 80;
  parseHostPort(endpoint, aiHost, aiPort);

  APP_SERIAL.printf("[AI/STREAM] Connecting to %s:%u (tls=%d)\n",
                    aiHost.c_str(), (unsigned)aiPort, useTls ? 1 : 0);

  const uint32_t connectTimeoutMs = useTls ? 15000 : 10000;
  if (!wifiClient->connect(aiHost.c_str(), aiPort, connectTimeoutMs)) {
    resp.error = String("Failed to connect to ") + providerDisplayName(cfg.aiProvider) +
                 " at " + aiHost + ":" + String(aiPort) + (useTls ? " (TLS)" : "");
    return false;
  }

  // For streaming we want a SHORT socket-read timeout. Each chunk arrives
  // within ~10-50ms of the previous one. A 30s ceiling means we still notice
  // a stalled connection but don't trip on the larger inference budget.
  const uint32_t chunkIdleTimeoutMs = 30000;
  wifiClient->setTimeout(chunkIdleTimeoutMs);

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(min(chunkIdleTimeoutMs, (uint32_t)60000));
  http.setConnectTimeout(connectTimeoutMs);

  if (!http.begin(*wifiClient, endpoint)) {
    resp.error = "Failed to initialize HTTP client.";
    wifiClient->stop();
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "text/event-stream");

  if (providerIsAnthropic(cfg.aiProvider)) {
    if (cfg.aiApiKey.length()) http.addHeader("x-api-key", cfg.aiApiKey);
    http.addHeader("anthropic-version", "2023-06-01");
  } else if (cfg.aiApiKey.length()) {
    http.addHeader("Authorization", String("Bearer ") + cfg.aiApiKey);
  }

  APP_SERIAL.printf("[AI/STREAM] POSTing to %s ...\n", endpoint.c_str());
  const uint32_t postStartMs = millis();

  int code = http.POST((uint8_t*)payload.c_str(), payload.length());
  resp.httpCode = code;
  payload.clear();

  if (code <= 0) {
    resp.error = String("HTTP POST failed: ") + http.errorToString(code);
    http.end();
    return false;
  }

  // For non-2xx, fall back to a small buffered read to get the error body
  if (code < 200 || code >= 300) {
    PsramString errBody;
    readResponseBody(http, errBody, 4096, 15000);
    http.end();
    resp.error = String(providerDisplayName(cfg.aiProvider)) +
                 " returned HTTP " + code + ": ";
    size_t errLen = errBody.length();
    if (errLen > 1024) errLen = 1024;
    resp.error += String(errBody.c_str()).substring(0, errLen);
    return false;
  }

  APP_SERIAL.printf("[AI/STREAM] Headers received in %.1fs, reading SSE stream...\n",
                    (millis() - postStartMs) / 1000.0f);

  // ---- SSE read loop ----
  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    resp.error = "No stream pointer available";
    http.end();
    return false;
  }

  const bool isAnthropic = providerIsAnthropic(cfg.aiProvider);

  // Line buffer — holds one SSE line at a time. SSE lines are typically
  // < 1 KB but can be larger if the model emits long deltas. 8 KB is safe.
  static constexpr size_t LINE_BUF_SZ = 8192;
  std::unique_ptr<char[]> lineBuf(new (std::nothrow) char[LINE_BUF_SZ]);
  if (!lineBuf) {
    resp.error = "Failed to allocate SSE line buffer";
    http.end();
    return false;
  }

  PsramString answer;
  answer.reserve(8192);

  String finishReason;
  bool usedReasoning = false;
  bool sawDone = false;
  bool wasCancelled = false;

  uint32_t lastByteMs = millis();
  uint32_t lastWdtMs  = millis();
  uint32_t lastCancelCheckMs = 0;
  size_t   totalBytes = 0;
  uint32_t chunkCount = 0;

  while (stream->connected() || stream->available()) {
    // ---- Cooperative cancel — checked at most every 100ms ----
    if (cb.shouldCancel && (millis() - lastCancelCheckMs > 100)) {
      lastCancelCheckMs = millis();
      if (cb.shouldCancel()) {
        APP_SERIAL.println("[AI/STREAM] Cancelled by caller");
        wasCancelled = true;
        stream->stop();
        break;
      }
    }

    // ---- Read one line ----
    int avail = stream->available();
    if (avail <= 0) {
      // No data right now — check for stall/timeout
      if (millis() - lastByteMs > chunkIdleTimeoutMs) {
        APP_SERIAL.printf("[AI/STREAM] No data for %ums, aborting\n",
                          (unsigned)(millis() - lastByteMs));
        resp.error = String(providerDisplayName(cfg.aiProvider)) +
                     " stream stalled (no chunk for " +
                     (chunkIdleTimeoutMs / 1000) + "s).";
        break;
      }
      // Feed WDT every second during idle waits
      if (millis() - lastWdtMs > 1000) {
        esp_task_wdt_reset();
        lastWdtMs = millis();
      }
      delay(5);
      continue;
    }

    // readBytesUntil includes the delimiter in its consume but not in the
    // returned data. Returns count of bytes read into buf (excluding '\n').
    size_t n = stream->readBytesUntil('\n', (uint8_t*)lineBuf.get(), LINE_BUF_SZ - 1);
    lineBuf[n] = '\0';
    lastByteMs = millis();
    totalBytes += n + 1; // +1 for the newline

    // Strip optional trailing CR
    if (n > 0 && lineBuf[n - 1] == '\r') {
      lineBuf[n - 1] = '\0';
      n--;
    }

    if (n == 0) continue;                       // blank line — chunk separator
    if (lineBuf[0] == ':')   continue;          // SSE comment / heartbeat
    if (strncmp(lineBuf.get(), "event:", 6) == 0) continue; // event name (we infer from payload)
    if (strncmp(lineBuf.get(), "data:", 5) != 0) continue;

    // Skip "data:" prefix and optional space
    const char* payloadPtr = lineBuf.get() + 5;
    size_t      payloadLen = n - 5;
    while (payloadLen > 0 && (*payloadPtr == ' ' || *payloadPtr == '\t')) {
      payloadPtr++;
      payloadLen--;
    }

    if (payloadLen == 0) continue;
    if (payloadLen == 6 && strncmp(payloadPtr, "[DONE]", 6) == 0) {
      sawDone = true;
      break;
    }

    // ---- Parse the chunk ----
    String delta;
    if (extractSseDelta(payloadPtr, payloadLen, isAnthropic,
                         delta, finishReason, usedReasoning)) {
      if (delta.length()) {
        answer.append(delta.c_str(), delta.length());
        chunkCount++;

        // Notify caller (under no lock — the callback handles its own
        // synchronization; in WebServerLL it grabs g_aiJobMutex)
        if (cb.onChunk) cb.onChunk(delta.c_str(), delta.length());
      }
    }

    // Progress callback every chunk (cheap — caller throttles internally)
    if (cb.onProgress) cb.onProgress(answer.length(), totalBytes);

    // Periodic WDT feed
    if (millis() - lastWdtMs > 1000) {
      esp_task_wdt_reset();
      lastWdtMs = millis();
    }
    vTaskDelay(1);

    // Hard cap on accumulated answer to prevent runaway streams
    if (answer.length() >= AI_RESPONSE_CHAR_LIMIT) {
      APP_SERIAL.printf("[AI/STREAM] Answer hit %u-char limit, stopping read\n",
                        (unsigned)AI_RESPONSE_CHAR_LIMIT);
      stream->stop();
      break;
    }
  }

  http.end();

  const uint32_t totalMs = millis() - postStartMs;
  APP_SERIAL.printf("[AI/STREAM] Done: %u chunks, %u answer chars, %u total bytes, %.1fs, done=%d, cancelled=%d\n",
                    (unsigned)chunkCount, (unsigned)answer.length(),
                    (unsigned)totalBytes, totalMs / 1000.0f,
                    sawDone ? 1 : 0, wasCancelled ? 1 : 0);

  resp.streamed = true;
  resp.bytesReceived = totalBytes;

  if (wasCancelled) {
    resp.cancelled = true;
    resp.ok = false;
    resp.answer = String(answer.c_str()); // partial answer up to cancel
    if (resp.error.length() == 0) resp.error = "Cancelled by user.";
    return false;
  }

  if (answer.length() == 0) {
    if (resp.error.length() == 0) {
      resp.error = String(providerDisplayName(cfg.aiProvider)) +
                   " stream returned no content.";
    }
    return false;
  }

  if (usedReasoning && !sawDone) {
    // Mirror the buffered path's behavior for reasoning-only outputs
    String prefixed = String(
        "[Note: The model used all its tokens on internal reasoning and "
        "produced no final answer. Below is the raw thinking output. "
        "Consider increasing max_tokens in config or using a non-thinking model.]\n\n");
    prefixed += String(answer.c_str());
    resp.answer = prefixed;
  } else {
    resp.answer = String(answer.c_str());
  }

  if (resp.answer.length() > AI_RESPONSE_CHAR_LIMIT) {
    resp.answer = resp.answer.substring(0, AI_RESPONSE_CHAR_LIMIT) + "\n...[response clipped]";
  }

  resp.ok = true;
  if (cb.onProgress) cb.onProgress(resp.answer.length(), resp.bytesReceived);

  if (finishReason.length()) {
    APP_SERIAL.printf("[AI/STREAM] finish_reason: %s\n", finishReason.c_str());
    if (finishReason == "length" || finishReason == "max_tokens") {
      APP_SERIAL.println("[AI/STREAM] WARNING: Model stopped at max_tokens limit. "
                         "Consider increasing aiMaxTokens in config.");
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// ask / askWithCallbacks — public dispatchers
// ---------------------------------------------------------------------------
//
// ask() is the legacy entry point: routes to streamed or buffered based on
// config, with no callbacks. askWithCallbacks() exposes progress/cancel
// hooks for UI integration (the WebServerLL job task uses these).
// ---------------------------------------------------------------------------

bool AiAssistant::ask(const AiAskRequest& req, AiAskResponse& resp) {
  return askWithCallbacks(req, resp, AiStreamCallbacks{});
}

bool AiAssistant::askWithCallbacks(const AiAskRequest& req, AiAskResponse& resp,
                                     const AiStreamCallbacks& cb) {
  DropdownConfig cfg = InventoryStore::getConfig();
  if (cfg.aiStreamingEnabled) {
    return askStreamed(req, resp, cb);
  }
  return askBuffered(req, resp, cb);
}

// ---------------------------------------------------------------------------
// askBuffered — original single-shot POST path (kept for compatibility)
// ---------------------------------------------------------------------------
//
// Used when cfg.aiStreamingEnabled is false. Sends the prompt, waits for the
// entire response in one shot, then parses. The TCP socket sees no bytes
// while the model thinks, so on slow generations this can trip the read
// timeout regardless of how high cfg.aiTimeoutSec is set.
// ---------------------------------------------------------------------------

bool AiAssistant::askBuffered(const AiAskRequest& req, AiAskResponse& resp,
                               const AiStreamCallbacks& cb) {
  DropdownConfig cfg = InventoryStore::getConfig();
  String baseUrl;
  if (!aiPreflight(cfg, baseUrl, resp.error, true)) return false;

  String validationError;
  resp.modelValidated = validateConfiguredModel(validationError);
  if (!resp.modelValidated) { resp.error = validationError; return false; }

  // ---- Build inventory context into PSRAM ----
  PsramString inventoryCtx;
  bool truncated = false, cacheHit = false;
  size_t itemsSerialized = 0, itemsTotal = 0;

  if (req.includeInventory) {
    buildInventoryContext(inventoryCtx, truncated, cacheHit, itemsSerialized, itemsTotal);
    resp.inventoryIncluded   = true;
    resp.inventoryChars      = inventoryCtx.length();
    resp.inventoryTruncated  = truncated;
    resp.inventoryCacheHit   = cacheHit;
    resp.inventoryItemCount  = itemsSerialized;
    resp.inventoryTotalItems = itemsTotal;
  }

  // ---- Resolve mode from config ----
  String mode = req.mode;
  mode.trim(); mode.toLowerCase();
  if (!mode.length()) mode = "general";
  resp.modeUsed = mode;

  // Look up mode in config.  If modes are empty (legacy config), use defaults.
  const std::vector<AiMode>& modes = cfg.aiModes.empty() ? defaultAiModes() : cfg.aiModes;
  const AiMode* am = findMode(modes, mode);
  // Fallback: if the requested mode doesn't exist, use first mode in list.
  if (!am && !modes.empty()) am = &modes[0];

  const bool restricted = am ? am->restrictToInventory : false;

  APP_SERIAL.printf("[AI] Mode: %s, restricted=%d\n", mode.c_str(), restricted ? 1 : 0);

  // ---- System prompt ----
  // Priority: per-mode systemPrompt > global aiSystemPrompt > built-in default.
  String systemPrompt;
  if (am && am->systemPrompt.length()) {
    systemPrompt = am->systemPrompt;
  } else {
    systemPrompt = cfg.aiSystemPrompt;
    systemPrompt.trim();
  }
  if (!systemPrompt.length()) {
    if (restricted) {
      systemPrompt =
        "You are LibationLocker AI, a bartender assistant. "
        "The user's inventory is provided in compact pipe-delimited format: "
        "type|brand|name|sizeMl|abv|qty|remain%|needBuy|hints|tags. "
        "A BY TYPE summary precedes the item lines. "
        "Answer using ONLY the provided inventory. "
        "Be concise and structured. Use bullets. Clearly label missing ingredients.";
    } else {
      systemPrompt =
        "You are LibationLocker AI, a knowledgeable bartender and cocktail assistant. "
        "Answer any question the user asks. You may provide drink recipes, general cocktail knowledge, "
        "food pairing advice, spirit education, or anything else. "
        "If the user's inventory is included, reference it when relevant but do not limit yourself to it. "
        "Be concise and structured. Use bullets where helpful.";
    }
  }

  // /no_think directive — LM Studio / Qwen / DeepSeek-R1 specific.
  // OpenAI and Anthropic don't recognize it (and OpenAI o1/o3 hide reasoning anyway).
  // Skip for cloud providers to avoid polluting the system prompt.
  if (cfg.aiDisableThinking && providerIsLmStudio(cfg.aiProvider)) {
    systemPrompt += " /no_think";
    APP_SERIAL.println("[AI] Thinking disabled via /no_think directive (LM Studio)");
  }

  // ---- Build user prompt into PSRAM ----
  PsramString userPrompt;
  userPrompt.reserve(req.question.length() + inventoryCtx.length() + 1024);
  userPrompt.append("Task mode: ");
  userPrompt.append(am ? am->label.c_str() : mode.c_str());
  userPrompt.append("\nInstruction: ");
  {
    // Use config instruction, or fall back to hardcoded modeInstruction()
    String inst = (am && am->instruction.length()) ? am->instruction : modeInstruction(mode);
    userPrompt.append(inst.c_str(), inst.length());
  }
  userPrompt.append("\n\nUser question:\n");
  userPrompt.append(req.question.c_str(), req.question.length());
  userPrompt.append("\n\n");

  if (req.includeInventory) {
    userPrompt.append(inventoryCtx.c_str(), inventoryCtx.length());
    if (restricted) {
      userPrompt.append("\nUse only this inventory snapshot. Clearly label missing ingredients.\n");
    } else {
      userPrompt.append("\nThe inventory above is provided for reference. Use it when relevant but you are not restricted to it.\n");
    }
    if (truncated) {
      userPrompt.append("NOTE: Inventory was truncated. State that limitation if it materially affects the answer.\n");
    }
  } else {
    userPrompt.append("No inventory snapshot included with this request.\n");
  }

  // ---- Build JSON payload directly into PSRAM ----
  // We manually construct JSON to avoid a second copy through ArduinoJson
  // serialization which would double memory usage for the prompt text.
  //
  // Two payload shapes:
  //   OpenAI / LM Studio / openai_compat:
  //     { model, temperature, max_tokens, messages: [
  //         {role:"system", content: "..."},
  //         {role:"user",   content: "..."}
  //     ] }
  //
  //   Anthropic:
  //     { model, max_tokens, temperature, system: "...",
  //       messages: [ {role:"user", content: "..."} ] }
  //     - system prompt is TOP-LEVEL, not in messages array
  //     - max_tokens is REQUIRED
  //     - anthropic-version header is required (added by transport)
  // ---- Token-field name varies per OpenAI model ----
  // Newer OpenAI models (o1, o3, gpt-4o-2024-08-06+, gpt-5*) require
  // "max_completion_tokens" instead of "max_tokens". Heuristic detection
  // by model id prefix; if uncertain, OpenAI rejects with a clear 400.
  auto needsMaxCompletionTokens = [](const String& model) -> bool {
    String lo = model; lo.toLowerCase();
    if (lo.startsWith("o1") || lo.startsWith("o3") || lo.startsWith("o4")) return true;
    if (lo.startsWith("gpt-5")) return true;
    return false;
  };

  PsramString payload;
  payload.reserve(systemPrompt.length() + userPrompt.length() + 1024);

  if (providerIsAnthropic(cfg.aiProvider)) {
    // Anthropic Messages API shape
    payload.append("{\"model\":\"");
    jsonEscapeInto(payload, cfg.aiModel.c_str(), cfg.aiModel.length());
    payload.append("\",\"max_tokens\":");
    payload.appendf("%d", cfg.aiMaxTokens);
    payload.append(",\"temperature\":");
    payload.appendf("%.2f", cfg.aiTemperature);
    payload.append(",\"system\":\"");
    jsonEscapeInto(payload, systemPrompt.c_str(), systemPrompt.length());
    payload.append("\",\"messages\":[{\"role\":\"user\",\"content\":\"");
    jsonEscapeInto(payload, userPrompt.c_str(), userPrompt.length());
    payload.append("\"}]}");
  } else {
    // OpenAI / LM Studio / openai_compat — Chat Completions shape
    const bool useMaxCompletion = providerIsOpenAI(cfg.aiProvider) &&
                                  needsMaxCompletionTokens(cfg.aiModel);

    payload.append("{\"model\":\"");
    jsonEscapeInto(payload, cfg.aiModel.c_str(), cfg.aiModel.length());

    // o1/o3 reject temperature too — they only accept default 1.0.
    // Skip the field for those models so the request doesn't 400.
    if (!useMaxCompletion) {
      payload.append("\",\"temperature\":");
      payload.appendf("%.2f", cfg.aiTemperature);
      payload.append(",\"max_tokens\":");
    } else {
      payload.append("\",\"max_completion_tokens\":");
    }
    payload.appendf("%d", cfg.aiMaxTokens);

    payload.append(",\"messages\":[{\"role\":\"system\",\"content\":\"");
    jsonEscapeInto(payload, systemPrompt.c_str(), systemPrompt.length());
    payload.append("\"},{\"role\":\"user\",\"content\":\"");
    jsonEscapeInto(payload, userPrompt.c_str(), userPrompt.length());
    payload.append("\"}]}");
  }

  APP_SERIAL.printf("[AI] Provider=%s, payload: %u chars, inventory: %u chars (%u/%u items), max_tokens=%d\n",
                    providerDisplayName(cfg.aiProvider),
                    (unsigned)payload.length(), (unsigned)inventoryCtx.length(),
                    (unsigned)itemsSerialized, (unsigned)itemsTotal, cfg.aiMaxTokens);

  // Free prompt buffers now — not needed after POST
  inventoryCtx.clear();
  userPrompt.clear();

  // ---- HTTP POST ----
  //
  // Transport selection:
  //   Plain TCP for LM Studio (HTTP) and openai_compat with http://
  //   TLS for OpenAI, Anthropic, and any openai_compat with https://
  //
  // ESP32 Arduino HTTPClient::setTimeout() accepts uint16_t in many core
  // versions, silently truncating values above 65535ms (e.g. 555s -> 31s).
  // Workaround: pre-connect the WiFiClient and set the socket timeout
  // directly. When HTTPClient::POST() calls its internal connect(), it
  // detects the client is already connected and skips the timeout override.
  //
  const uint32_t httpTimeoutMs = (uint32_t)cfg.aiTimeoutSec * 1000;
  const String endpoint = providerIsAnthropic(cfg.aiProvider)
                          ? (baseUrl + "/v1/messages")
                          : (baseUrl + "/v1/chat/completions");

  const bool useTls = providerIsHttps(baseUrl, cfg.aiProvider);
  auto wifiClient = makeClient(useTls, cfg.aiTlsInsecure);

  // Pre-connect: establish TCP/TLS before HTTPClient touches the socket
  String aiHost;
  uint16_t aiPort = useTls ? 443 : 80;
  parseHostPort(endpoint, aiHost, aiPort);

  APP_SERIAL.printf("[AI] Connecting to %s:%u (tls=%d)\n",
                    aiHost.c_str(), (unsigned)aiPort, useTls ? 1 : 0);

  // TLS handshake takes longer than plain TCP — 15s is generous but safe
  const uint32_t connectTimeoutMs = useTls ? 15000 : 10000;
  if (!wifiClient->connect(aiHost.c_str(), aiPort, connectTimeoutMs)) {
    resp.error = String("Failed to connect to ") + providerDisplayName(cfg.aiProvider) +
                 " at " + aiHost + ":" + String(aiPort) +
                 (useTls ? " (TLS)" : "");
    return false;
  }

  // Set the REAL socket timeout — no uint16_t truncation
  wifiClient->setTimeout(httpTimeoutMs);

  HTTPClient http;
  http.setReuse(false);

  // Cap HTTPClient's internal value to prevent overflow.
  // Since the client is already connected, POST() won't overwrite our timeout.
  http.setTimeout(min(httpTimeoutMs, (uint32_t)60000));
  http.setConnectTimeout(connectTimeoutMs);

  if (!http.begin(*wifiClient, endpoint)) {
    resp.error = "Failed to initialize HTTP client.";
    wifiClient->stop();
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  // Provider-specific auth headers
  if (providerIsAnthropic(cfg.aiProvider)) {
    if (cfg.aiApiKey.length()) {
      http.addHeader("x-api-key", cfg.aiApiKey);
    }
    http.addHeader("anthropic-version", "2023-06-01");
  } else if (cfg.aiApiKey.length()) {
    http.addHeader("Authorization", String("Bearer ") + cfg.aiApiKey);
  }

  APP_SERIAL.printf("[AI] POSTing to %s (timeout=%us, socket=%ums) ...\n",
                    endpoint.c_str(), (unsigned)cfg.aiTimeoutSec, (unsigned)httpTimeoutMs);
  const uint32_t postStartMs = millis();

  int code = http.POST((uint8_t*)payload.c_str(), payload.length());
  resp.httpCode = code;

  const uint32_t postElapsedMs = millis() - postStartMs;
  APP_SERIAL.printf("[AI] POST returned %d in %.1f seconds\n", code, postElapsedMs / 1000.0f);

  // Free payload immediately after POST sends
  payload.clear();

  if (code <= 0) {
    if (code == -11) {  // HTTPC_ERROR_READ_TIMEOUT
      resp.error = String(providerDisplayName(cfg.aiProvider)) +
                   " did not respond within " + cfg.aiTimeoutSec +
                   "s. Increase AI Timeout in config, or reduce inventory size / max_tokens.";
    } else {
      resp.error = String("HTTP POST failed: ") + http.errorToString(code);
    }
    http.end();
    return false;
  }

  // ---- Read response body into PSRAM ----
  PsramString body;
  if (code >= 200 && code < 300) {
    readResponseBody(http, body, AI_RESPONSE_READ_LIMIT, httpTimeoutMs, cb.shouldCancel);
    if (cb.shouldCancel && cb.shouldCancel()) {
      resp.cancelled = true;
      resp.error = "Cancelled by user.";
      http.end();
      return false;
    }
  } else {
    // For error responses, read a smaller chunk
    readResponseBody(http, body, 4096, 15000);
  }
  http.end();

  APP_SERIAL.printf("[AI] Response: HTTP %d, %u bytes\n", code, (unsigned)body.length());

  if (code < 200 || code >= 300) {
    resp.error = String(providerDisplayName(cfg.aiProvider)) +
                 " returned HTTP " + code + ": ";
    size_t errBodyLen = body.length();
    if (errBodyLen > 1024) errBodyLen = 1024;
    resp.error += String(body.c_str()).substring(0, errBodyLen);
    return false;
  }

  // ---- Parse JSON response ----
  PsramJsonDocument doc(AI_DOC_CAPACITY);
  DeserializationError err = deserializeJson(doc, body.c_str(), body.length());

  if (err) {
    APP_SERIAL.printf("[AI] JSON parse failed: %s\n", err.c_str());
    APP_SERIAL.printf("[AI] Raw body (first 500 chars): %.500s\n", body.c_str());
    resp.error = String("Failed parsing AI response: ") + err.c_str();
    body.clear();
    return false;
  }

  // ---- Extract answer text — provider-specific ----
  String answer;
  bool usedReasoning = false;
  String finishReasonStr;

  if (providerIsAnthropic(cfg.aiProvider)) {
    // Anthropic Messages API:
    //   { id, type:"message", role:"assistant",
    //     content: [ {type:"text", text:"..."}, ... ],
    //     stop_reason: "end_turn" | "max_tokens" | "stop_sequence" | "tool_use" }
    JsonArrayConst contentArr = doc["content"].as<JsonArrayConst>();
    if (!contentArr.isNull()) {
      for (JsonVariantConst c : contentArr) {
        String t = c["type"] | "";
        if (t == "text") {
          String txt = c["text"] | "";
          if (txt.length()) {
            if (answer.length()) answer += "\n";
            answer += txt;
          }
        }
      }
    } else {
      APP_SERIAL.println("[AI] Anthropic response missing 'content' array");
    }

    finishReasonStr = doc["stop_reason"] | "";

    // Anthropic surfaces errors as { type: "error", error: { type, message } }
    if (!answer.length()) {
      String errType = doc["error"]["type"] | "";
      String errMsg  = doc["error"]["message"] | "";
      if (errType.length() || errMsg.length()) {
        APP_SERIAL.printf("[AI] Anthropic error: %s — %s\n", errType.c_str(), errMsg.c_str());
        resp.error = String("Anthropic error: ") + errType + " — " + errMsg;
        body.clear();
        return false;
      }
    }
  } else {
    // OpenAI / LM Studio / openai_compat — Chat Completions shape
    JsonArrayConst choices = doc["choices"].as<JsonArrayConst>();
    if (!choices.isNull() && choices.size() > 0) {
      JsonObjectConst msg = choices[0]["message"].as<JsonObjectConst>();
      if (!msg.isNull()) {
        answer = flattenContent(msg["content"]);

        // Fallback: reasoning_content — used by Qwen 3.x / DeepSeek-R1 in LM Studio.
        // OpenAI o1/o3 hide reasoning entirely so this branch only fires on LM Studio.
        if (!answer.length()) {
          String reasoning = msg["reasoning_content"] | "";
          if (reasoning.length()) {
            APP_SERIAL.printf("[AI] content empty but reasoning_content has %u chars — using as answer\n",
                             (unsigned)reasoning.length());
            answer = reasoning;
            usedReasoning = true;
          }
        }
      }
    }

    // Fallback: OpenAI Responses API format (in case openai_compat returns it)
    if (!answer.length()) answer = doc["output_text"] | "";
    if (!answer.length()) {
      JsonArrayConst output = doc["output"].as<JsonArrayConst>();
      if (!output.isNull()) {
        for (JsonVariantConst entry : output) {
          JsonArrayConst contentArr = entry["content"].as<JsonArrayConst>();
          if (contentArr.isNull()) continue;
          for (JsonVariantConst c : contentArr) {
            String t = c["type"] | "";
            if (t == "output_text" || t == "text") {
              String txt = c["text"] | "";
              if (txt.length()) {
                if (answer.length()) answer += "\n";
                answer += txt;
              }
            }
          }
        }
      }
    }

    if (!choices.isNull() && choices.size() > 0) {
      finishReasonStr = choices[0]["finish_reason"] | "";
    }
  }

  if (!answer.length()) {
    APP_SERIAL.println("[AI] ERROR: No readable answer extracted. Raw response (first 800 chars):");
    APP_SERIAL.printf("%.800s\n", body.c_str());
    String snippet = String(body.c_str()).substring(0, 300);
    resp.error = String(providerDisplayName(cfg.aiProvider)) +
                 " response did not contain a readable answer. Response preview: " + snippet;
    body.clear();
    return false;
  }

  if (usedReasoning) {
    answer = String("[Note: The model used all its tokens on internal reasoning and "
                    "produced no final answer. Below is the raw thinking output. "
                    "Consider increasing max_tokens in config or using a non-thinking model.]\n\n") + answer;
  }

  body.clear();

  if (answer.length() > AI_RESPONSE_CHAR_LIMIT) {
    answer = answer.substring(0, AI_RESPONSE_CHAR_LIMIT) + "\n...[response clipped]";
  }

  resp.ok = true;
  resp.answer = answer;
  resp.streamed = false;
  resp.bytesReceived = body.length();

  // Final progress callback so UIs that poll bytesReceived see completion
  if (cb.onProgress) cb.onProgress(answer.length(), resp.bytesReceived);

  if (finishReasonStr.length()) {
    APP_SERIAL.printf("[AI] finish_reason: %s\n", finishReasonStr.c_str());
    if (finishReasonStr == "length" || finishReasonStr == "max_tokens") {
      APP_SERIAL.println("[AI] WARNING: Model stopped at max_tokens limit. "
                         "Consider increasing aiMaxTokens in config.");
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// testConnection (unchanged logic)
// ---------------------------------------------------------------------------

bool AiAssistant::testConnection(String& message) {
  String validationError;
  if (!validateConfiguredModel(validationError)) {
    message = validationError;
    return false;
  }
  AiAskRequest req;
  req.question = "Reply with exactly: LibationLocker AI connection OK.";
  req.includeInventory = false;
  req.mode = "general";

  AiAskResponse resp;
  bool ok = ask(req, resp);
  if (ok) { message = resp.answer; return true; }
  message = resp.error.length() ? resp.error : "Unknown AI test failure.";
  return false;
}
