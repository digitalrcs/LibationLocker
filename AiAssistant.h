#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>

// ---------------------------------------------------------------------------
// PsramString — RAII char buffer allocated in PSRAM (fallback: heap).
// NOT a full String replacement.  Use for large transient buffers that must
// stay off the ~300 KB internal heap.
//
// Usage:
//   PsramString buf(65536);          // allocates 64 KB in PSRAM
//   buf.append("hello");             // append chars, grows nothing
//   buf.appendf("val=%d", 42);       // printf-style append
//   const char* p = buf.c_str();     // null-terminated read
//   size_t n      = buf.length();    // bytes written so far
// ---------------------------------------------------------------------------
struct PsramString {
  char*  data     = nullptr;
  size_t capacity = 0;
  size_t len      = 0;

  explicit PsramString(size_t cap = 0) {
    if (cap) reserve(cap);
  }
  ~PsramString() { if (data) free(data); }

  PsramString(const PsramString&) = delete;
  PsramString& operator=(const PsramString&) = delete;

  // Move support
  PsramString(PsramString&& o) noexcept
      : data(o.data), capacity(o.capacity), len(o.len) {
    o.data = nullptr; o.capacity = 0; o.len = 0;
  }
  PsramString& operator=(PsramString&& o) noexcept {
    if (this != &o) {
      if (data) free(data);
      data = o.data; capacity = o.capacity; len = o.len;
      o.data = nullptr; o.capacity = 0; o.len = 0;
    }
    return *this;
  }

  bool reserve(size_t cap) {
    if (cap <= capacity) return true;
    cap = ((cap + 255) / 256) * 256;           // round up to 256-byte blocks
    char* p = (char*)(psramFound() ? ps_malloc(cap) : malloc(cap));
    if (!p) return false;
    if (data) { memcpy(p, data, len); free(data); }
    data = p;
    capacity = cap;
    data[len] = '\0';
    return true;
  }

  void clear() { len = 0; if (data) data[0] = '\0'; }

  bool append(const char* s, size_t sLen) {
    if (!s || !sLen) return true;
    if (len + sLen + 1 > capacity) {
      if (!reserve((len + sLen + 1) * 3 / 2)) return false;
    }
    memcpy(data + len, s, sLen);
    len += sLen;
    data[len] = '\0';
    return true;
  }
  bool append(const char* s)    { return s ? append(s, strlen(s)) : true; }
  bool append(const String& s)  { return append(s.c_str(), s.length()); }
  bool append(char c)           { return append(&c, 1); }

  bool appendf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    return append(tmp, (size_t)n);
  }

  const char* c_str()  const { return data ? data : ""; }
  size_t      length() const { return len; }
  bool        empty()  const { return len == 0; }
};

// ---------------------------------------------------------------------------
struct AiAskRequest {
  String question;
  bool   includeInventory = true;
  String mode = "general"; // general|can_make_now|missing_ingredients|recommend_purchases|shopping_list
};

struct AiModelInfo {
  String id;
  String objectType;
  bool   ownedByLmStudio = false;
};

struct AiAskResponse {
  bool   ok                  = false;
  String answer;
  String error;
  int    httpCode            = 0;
  bool   inventoryIncluded   = false;
  bool   inventoryTruncated  = false;
  bool   inventoryCacheHit   = false;
  bool   modelValidated      = false;
  String modeUsed;
  size_t inventoryChars      = 0;
  size_t inventoryItemCount  = 0;   // NEW — items actually serialized
  size_t inventoryTotalItems = 0;   // NEW — total items in store
  bool   streamed            = false; // true if SSE path was used
  bool   cancelled           = false; // true if cancellation flag aborted the call
  size_t bytesReceived       = 0;
};

// ---------------------------------------------------------------------------
// AiStreamCallbacks — owned by the caller. All callbacks may run on a
// FreeRTOS task other than the UI/web task. Keep them short and lock-safe.
//
//   onChunk        — called for every text fragment as it arrives. The
//                    string is just the new delta (not the cumulative answer).
//   shouldCancel   — polled between chunks. Return true to abort the request.
//   onProgress     — called with cumulative answer length and bytes from the
//                    socket. Use for progress UI without re-copying answer.
//
// All three are optional; pass {} to use buffered behavior with no progress.
// ---------------------------------------------------------------------------
struct AiStreamCallbacks {
  std::function<void(const char* delta, size_t deltaLen)> onChunk;
  std::function<bool()>                                   shouldCancel;
  std::function<void(size_t answerLen, size_t bytesReceived)> onProgress;
};

class AiAssistant {
public:
  // Backwards-compatible synchronous call. Internally chooses streaming or
  // buffered transport based on cfg.aiStreamingEnabled.
  static bool ask(const AiAskRequest& req, AiAskResponse& resp);

  // Streaming-aware call: invokes callbacks as bytes/chunks arrive, and
  // checks shouldCancel between chunks. resp.answer holds the full answer
  // when the call returns successfully. If cancelled, resp.cancelled = true
  // and resp.answer holds whatever was received so far.
  static bool askWithCallbacks(const AiAskRequest& req, AiAskResponse& resp,
                                const AiStreamCallbacks& cb);

  static bool testConnection(String& message);
  static bool listModels(std::vector<AiModelInfo>& models, String& error, bool forceRefresh = false);
  static bool validateConfiguredModel(String& error);

  static String normalizeBaseUrl(const String& raw);

  // Public escape helper — exposed so the payload builder helper in
  // AiAssistant.cpp can use it without befriending. Safe to call from
  // anywhere; does not touch any class state.
  static void jsonEscapeInto(PsramString& out, const char* s, size_t sLen);

private:
  static bool buildInventoryContext(PsramString& out, bool& truncated, bool& cacheHit,
                                    size_t& itemsSerialized, size_t& itemsTotal);
  static String jsonEscape(const String& s);
  static String flattenContent(const JsonVariantConst& content);
  static String normalizedHintsForItem(const String& type, const String& brand,
                                       const String& name, const std::vector<String>& tags);
  static String modeInstruction(const String& mode);
  static uint32_t inventorySignature();

  // Internal: actual transport implementations. Both build the payload from
  // req+cfg and write the final answer/error into resp.
  static bool askBuffered(const AiAskRequest& req, AiAskResponse& resp,
                          const AiStreamCallbacks& cb);
  static bool askStreamed(const AiAskRequest& req, AiAskResponse& resp,
                          const AiStreamCallbacks& cb);
};
