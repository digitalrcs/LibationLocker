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

// ---------------------------------------------------------------------------
// AiMode — a user-configurable AI interaction mode.
//
//   id                 Machine name used in the API (e.g., "general").
//   label              Human-readable display name shown in the UI dropdown.
//   instruction        Instruction text injected into the user prompt.
//   systemPrompt       Optional per-mode system prompt override.
//                      If empty, the global aiSystemPrompt (or built-in default) is used.
//   restrictToInventory  When true the prompt tells the model to answer using
//                        ONLY the provided inventory.  When false the inventory
//                        is included as optional context.
// ---------------------------------------------------------------------------
struct AiMode {
  String id;
  String label;
  String instruction;
  String systemPrompt;                // empty = use global default
  bool   restrictToInventory = true;
};

// Returns the 5 built-in default modes.  Called when no modes exist in
// persisted config so the user always starts with a working set.
inline std::vector<AiMode> defaultAiModes() {
  std::vector<AiMode> v;

  { AiMode m;
    m.id = "general";
    m.label = "General";
    m.instruction = "Answer the question freely. You may provide recipes, cocktail knowledge, spirit education, purchase advice, or anything else the user asks about.";
    m.restrictToInventory = false;
    v.push_back(m);
  }
  { AiMode m;
    m.id = "can_make_now";
    m.label = "Can Make Now";
    m.instruction = "List cocktails/drinks that can be made immediately from current inventory only. Short list with reasoning.";
    m.restrictToInventory = true;
    v.push_back(m);
  }
  { AiMode m;
    m.id = "missing_ingredients";
    m.label = "Missing Ingredients";
    m.instruction = "Focus on drinks that are nearly possible. List what is missing bottle-by-bottle.";
    m.restrictToInventory = true;
    v.push_back(m);
  }
  { AiMode m;
    m.id = "recommend_purchases";
    m.label = "Recommend Purchases";
    m.instruction = "Recommend highest-value purchases that unlock the most cocktails based on current inventory.";
    m.restrictToInventory = true;
    v.push_back(m);
  }
  { AiMode m;
    m.id = "shopping_list";
    m.label = "Shopping List";
    m.instruction = "Build a practical shopping list grouped into: bottles, mixers, citrus, syrups, optional extras.";
    m.restrictToInventory = true;
    v.push_back(m);
  }

  return v;
}

// AI provider selector.
//   lmstudio       — Local LM Studio (HTTP, OpenAI-compatible). Default.
//   openai         — OpenAI api.openai.com (HTTPS, /v1/chat/completions)
//   anthropic      — Anthropic api.anthropic.com (HTTPS, /v1/messages, x-api-key)
//   openai_compat  — Any OpenAI-compatible HTTPS endpoint (Groq, Together,
//                    OpenRouter, Mistral, Gemini OpenAI-compat, etc.)
//
// These are stored as strings (not enums) so the UI/config round-trips cleanly
// and unknown values fall back to lmstudio without breaking persistence.
//
// Default base URLs per provider (used by the UI when you switch provider):
//   lmstudio       -> http://<host>:1234
//   openai         -> https://api.openai.com
//   anthropic      -> https://api.anthropic.com
//   openai_compat  -> (user supplies)
struct DropdownConfig {
  std::vector<String> types;
  std::vector<int>    sizesMl;
  std::vector<float>  abvPresets;
  std::vector<int>    remainingPresets;

  bool   aiEnabled = false;
  String aiProvider = "lmstudio"; // lmstudio | openai | anthropic | openai_compat
  String aiBaseUrl = "http://192.168.5.250:1234";
  String aiModel;
  String aiApiKey;
  String aiSystemPrompt;       // global default system prompt
  float  aiTemperature = 0.2f;
  int    aiMaxTokens = 8192;
  int    aiTimeoutSec = 180;
  bool   aiDisableThinking = true;

  // ---- Streaming (SSE) mode ----
  // When true, AiAssistant::ask() uses Server-Sent Events with stream:true.
  // Tokens arrive every 10-50ms so the TCP socket never goes idle, which
  // eliminates HTTPClient read-timeout problems on long generations.
  // Also enables progressive UI updates and cooperative cancellation.
  // Set false to fall back to the old single-shot buffered POST.
  bool   aiStreamingEnabled = true;

  // TLS verification mode for HTTPS providers.
  //   true  = setInsecure() — skip cert verification. API key is the auth.
  //   false = use built-in CA bundle (when available) or pinned cert.
  // Default true because: ESP32-S3 has limited room for CA bundles, and
  // the API key in Authorization/x-api-key is the real auth layer.
  bool   aiTlsInsecure = true;

  std::vector<AiMode> aiModes; // user-configurable modes
};
