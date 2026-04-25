# Libation Locker by Dan Roberts (ESP32-S3)

LibationLocker is a **local, self-hosted inventory web app** for tracking spirits / bottles. It runs entirely on an ESP32-S3 and exposes:

- A single-page web UI (served from flash, embedded in `WebUiAssets.h`)
- A small JSON REST API
- LittleFS persistence (inventory + dropdown + AI config)
- An on-device AI assistant that supports multiple backends:
  - Local **LM Studio** (default, no API key, nothing leaves your network)
  - **OpenAI** (api.openai.com — ChatGPT models, GPT-4o, o1, o3, etc.)
  - **Anthropic** (api.anthropic.com — Claude Sonnet, Opus, Haiku)
  - **OpenAI-compatible** (Groq, Together, OpenRouter, Mistral, Gemini's OpenAI-compat endpoint, self-hosted vLLM, etc.)

> The AI side is fully optional. You can disable it in config and run the app exactly as before.

---

## Table of Contents

1. [Features](#features)
2. [Hardware / Build Notes](#hardware--build-notes)
3. [Networking](#networking)
4. [Security & Default Credentials](#security--default-credentials)
5. [REST API — Inventory & Config](#rest-api--inventory--config)
6. [AI Assistant](#ai-assistant)
7. [Persistence Details](#persistence-details)
8. [Troubleshooting](#troubleshooting)
9. [Repo Layout](#repo-layout)
10. [Contributing](#contributing)

---

## Features

- **AP-first** operation (device always creates its own Wi-Fi AP)
- Optional **STA join** to your home Wi-Fi (AP+STA)
- mDNS: browse to **`http://libationlocker.local/`** when on the same LAN
- Inventory CRUD (type / brand / name / size / ABV / qty / remaining / need / rating / tags / notes)
- Optimistic concurrency on update (server-side `version` field)
- Import / Export (JSON / CSV / TXT) with `merge | append | replace` modes and dry-run
- Shopping-list extract: export only items where `needToBuy=true`
- HTTP Basic Auth on every UI/API route
- **AI assistant** (LM Studio backend) with:
  - Async job pattern (one in-flight job, status polling, no AsyncTCP blocking)
  - Five built-in modes (`general`, `can_make_now`, `missing_ingredients`, `recommend_purchases`, `shopping_list`)
  - User-editable modes (id / label / instruction / per-mode system prompt / inventory restriction flag)
  - PSRAM-backed prompt + response buffers (off the 300 KB internal heap)
  - Compact pipe-delimited inventory serialization
  - Inventory-signature cache (skip re-serialization when nothing changed)
  - Model-list cache (30 s TTL)
  - `/no_think` directive for Qwen-style reasoning models
  - Configurable timeout up to 600 s for slow CPU inference
- Heap-health watchdog logging (warn at 30 KB, critical at 15 KB)

---

## Hardware / Build Notes

### Recommended boards

- ESP32-S3 with PSRAM (e.g. **N16R8 = 16 MB flash / 8 MB PSRAM**)

PSRAM is **required** if you intend to use the AI assistant. Without it, large prompts and responses will exhaust the internal heap.

### Arduino IDE settings (typical)

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| PSRAM | Enabled (OPI if available) |
| Partition Scheme | Custom (uses bundled `partitions.csv`) |
| Erase Flash (first time) | All Flash Contents |

### Partition table

The bundled `partitions.csv` provides a large LittleFS partition.

> The filesystem partition is labeled **`spiffs`** (subtype is still **littlefs**). This matches Arduino-ESP32 defaults so `LittleFS.begin()` mounts cleanly without explicit args.

### Library dependencies

- `ESPAsyncWebServer` (mathieucarbou or me-no-dev fork)
- `AsyncTCP`
- `ArduinoJson` (v6.x)
- `LittleFS` (bundled with Arduino-ESP32 core)
- `HTTPClient` (bundled)
- `WiFi` (bundled)

---

## Networking

### Access methods

- **AP mode** (always on): connect to the device's Wi-Fi AP and browse to `http://192.168.4.1/`
- **STA mode** (optional): once joined to your LAN, browse to `http://libationlocker.local/` or the IP shown by `GET /api/net`

### Configure STA join

```bash
curl -u admin:admin -X PUT http://192.168.4.1/api/net \
  -H "Content-Type: application/json" \
  -d '{"enabled":true,"ssid":"YourWifi","pass":"YourPassword"}'
```

The device joins, advertises mDNS, and is reachable on both networks simultaneously.

---

## Security & Default Credentials

> **Read this section before you put the device on a shared network.** The shipped defaults are intentionally weak so the firmware is usable on first boot. Change them.

### Defaults baked into `AppConfig.h`

| What | Default |
|---|---|
| Wi-Fi AP SSID | `LibationLocker` |
| Wi-Fi AP password | `adminadmin` |
| Web Basic Auth user | `admin` |
| Web Basic Auth password | `admin` |

`AppConfig::begin()` enforces a minimum 8-char AP password — if the compiled-in value is shorter, it falls back to `adminadmin`. There is **no auto-generation from MAC** in the current code.

### Changing credentials

Edit `AppConfig.h` and recompile:

```cpp
struct RuntimeConfig {
  String apSsid  = "LibationLocker";
  String apPass  = "your-strong-ap-password";   // >= 8 chars
  String webUser = "your-admin-user";           // empty = disable Basic Auth
  String webPass = "your-strong-admin-password";
  // ...
};
```

If `webUser` is empty, Basic Auth is disabled entirely.

### Transport security

All traffic is **HTTP (no TLS)**. Use only on networks you trust. Basic Auth credentials are protected by the AP/Wi-Fi WPA2 layer, nothing more.

---

## REST API — Inventory & Config

Base URL:

- AP mode: `http://192.168.4.1`
- STA mode: `http://libationlocker.local` (or IP from `/api/net`)

All requests require Basic Auth (unless `webUser` is empty in config).

All responses are JSON unless noted (export endpoints stream files).

### Health

#### `GET /api/health`

```bash
curl -u admin:admin http://192.168.4.1/api/health
# {"ok":true}
```

### Network status + config

#### `GET /api/net`

```json
{
  "mode": "AP+optional STA",
  "ap":   { "ssid": "LibationLocker", "ip": "192.168.4.1" },
  "sta":  { "connected": false, "ssid": "", "ip": "", "rssi": 0 },
  "cfg":  { "enabled": false, "ssid": "" }
}
```

#### `PUT /api/net`

```json
{ "enabled": true, "ssid": "YourWifi", "pass": "YourPassword" }
```

### Storage / capacity

#### `GET /api/storage`

Returns heap + filesystem usage and a rough item-capacity estimate.

Key fields: `heapTotalBytes`, `heapUsedBytes`, `heapFreeBytes`, `totalBytes`, `usedBytes`, `freeBytes`, `fsMounted`, `avgBytesPerItem`, `estItemsLeft`, `lastError`.

### Items

#### Item schema

```json
{
  "id": "abc123",
  "type": "Bourbon",
  "brand": "Maker's Mark",
  "name": "46",
  "sizeMl": 750,
  "abv": 45.0,
  "qty": 1,
  "remainingPct": 60,
  "needToBuy": false,
  "rating": 4,
  "tags": ["oak", "vanilla"],
  "notes": "good neat",
  "updatedAt": 1730000000,
  "version": 3
}
```

- `abv` is optional (`NaN`-equivalent — field omitted when unknown)
- `version` is used for **optimistic concurrency** on update
- `rating` ranges 0..10

#### `GET /api/items`

Returns an array of all items.

#### `POST /api/item`

Creates an item. `id` in the body is ignored — server generates one. Required fields: `type`, `brand`, `name`.

#### `PUT /api/item`

Updates an item (by `id` in body).

| Code | Meaning |
|---|---|
| 200 | Updated; returns updated item |
| 404 | `{"error":"not_found"}` |
| 409 | `{"error":"version_conflict","current":{...}}` — re-fetch and retry |

#### `DELETE /api/item?id=<id>`

```bash
curl -u admin:admin -X DELETE "http://192.168.4.1/api/item?id=abc123"
```

### Dropdown + AI configuration

The single config endpoint covers **both** UI dropdown presets and the AI configuration. The AI block is optional — sending a config without an `ai` key leaves AI settings untouched on the server side only if you read-modify-write; an explicit `PUT` replaces the full document.

#### `GET /api/config`

```json
{
  "types": ["Bourbon", "Rye", "Gin", "Tequila"],
  "sizesMl": [50, 375, 750, 1000, 1750],
  "abvPresets": [40, 45, 50],
  "remainingPresets": [100, 75, 50, 25, 0],

  "ai": {
    "enabled": false,
    "baseUrl": "http://192.168.5.250:1234",
    "model": "qwen2.5-7b-instruct",
    "apiKey": "",
    "systemPrompt": "",
    "temperature": 0.2,
    "maxTokens": 8192,
    "timeoutSec": 180,
    "disableThinking": true,
    "modes": [
      {
        "id": "general",
        "label": "General",
        "instruction": "Answer the question freely...",
        "systemPrompt": "",
        "restrictToInventory": false
      }
    ],
    "availableModels": []
  }
}
```

#### `PUT /api/config`

Body has the same shape. The server clamps:

- `temperature` to `[0.0, 2.0]`
- `maxTokens` to `[64, 16384]`
- `timeoutSec` to `[30, 600]`

If `modes` is empty or missing, the five built-in defaults are seeded automatically.

### Filesystem listing (debug)

#### `GET /api/fs`

Returns directory entries for `/` and `/data`.

### Export

#### `GET /api/export`

| Query param | Values | Default |
|---|---|---|
| `format` | `json`, `csv`, `txt` | `json` |
| `filter` | `need` / `shopping` / `needtobuy` | (none) |

```bash
# Full bundle (config + items)
curl -u admin:admin -L "http://192.168.4.1/api/export?format=json" -o backup.json

# Shopping list, plain text
curl -u admin:admin -L "http://192.168.4.1/api/export?format=txt&filter=need" -o shop.txt
```

### Import

#### `POST /api/import`

| Query param | Values | Default |
|---|---|---|
| `mode` | `merge`, `append`, `replace` | `merge` |
| `dryrun` | `1` / `true` / `yes` | off |

Body: a JSON bundle of the shape produced by `GET /api/export?format=json`.

Response:

```json
{ "ok": true, "mode": "merge", "dryRun": false,
  "addCount": 3, "updateCount": 2, "conflictCount": 0 }
```

---

## AI Assistant

The AI assistant supports four backends, selected via the `ai.provider` field. All four use the same prompt-building, mode system, inventory-context machinery, and async job pattern — only the wire format and transport differ.

| Provider id | Backend | Transport | Auth | Default base URL |
|---|---|---|---|---|
| `lmstudio` (default) | Local LM Studio | HTTP | none | user-supplied (e.g. `http://192.168.5.250:1234`) |
| `openai` | OpenAI ChatGPT | HTTPS | `Authorization: Bearer` | `https://api.openai.com` |
| `anthropic` | Anthropic Claude | HTTPS | `x-api-key` + `anthropic-version` | `https://api.anthropic.com` |
| `openai_compat` | Any OpenAI-compatible host | HTTP or HTTPS | `Authorization: Bearer` | user-supplied |

Pick `lmstudio` if you want **everything local, nothing leaves your network, no API costs**. Pick `openai` or `anthropic` if you want a smarter / faster cloud model and have an API key. `openai_compat` covers Groq, Together, OpenRouter, Mistral, Gemini's OpenAI-compat endpoint, and self-hosted vLLM/Ollama OpenAI shims.

See the [AI Multi-Provider wiki page](https://github.com/.../wiki/AI-Multi-Provider) for full per-provider setup, costs, and trade-offs.

### Architecture overview

```
Browser  ──HTTP+BasicAuth──>  ESP32-S3
                                │
                                ├── /api/ai/test     (start "test" job)
                                ├── /api/ai/ask      (start "ask" job)
                                ├── /api/ai/status   (poll job state)
                                └── /api/ai/models   (list LM Studio models)
                                              │
                                              ▼
                              FreeRTOS task "ai_job"  (core 1, 32 KB stack)
                                              │
                                              ▼
                                     LM Studio HTTP server
                                     POST /v1/chat/completions
                                     GET  /v1/models
```

Why a separate task? `AsyncWebServer` runs on the AsyncTCP task and **must never block**. Inference can take 30–300 seconds on a CPU. So the HTTP handlers immediately return `202 Accepted` with a **token**, and the work happens in a pinned FreeRTOS task. The browser polls `/api/ai/status` and renders the answer when `done=true`.

Only **one** AI job runs at a time. A second `POST /api/ai/ask` while one is in flight returns `409` with the existing token.

### LM Studio setup (one-time)

1. Install **LM Studio** on a desktop / NAS / mini-PC on the same LAN as the ESP32.
2. Download a model. Recommended starting points:
   - `qwen2.5-7b-instruct` (general, fast)
   - `llama-3.1-8b-instruct` (general)
   - Any `*-coder-*` only if you want code in your cocktail recipes (you don't)
3. In LM Studio → **Server** tab → enable the local server.
   - Default URL: `http://<host-ip>:1234`
   - Make sure it binds to **`0.0.0.0`**, not `127.0.0.1`, so the ESP32 can reach it.
4. Note the host IP. The ESP32 will hit `http://<host-ip>:1234/v1/chat/completions` and `/v1/models`.

### Configure the ESP32 side

Either through the web UI's settings panel, or directly via the API:

```bash
curl -u admin:admin -X PUT http://libationlocker.local/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "types":["Bourbon","Rye","Gin","Tequila"],
    "sizesMl":[375,750,1000,1750],
    "abvPresets":[40,45,50],
    "remainingPresets":[100,75,50,25,0],
    "ai":{
      "enabled": true,
      "baseUrl": "http://192.168.5.250:1234",
      "model": "qwen2.5-7b-instruct",
      "temperature": 0.2,
      "maxTokens": 8192,
      "timeoutSec": 180,
      "disableThinking": true
    }
  }'
```

### AI configuration fields

| Field | Type | Default | Notes |
|---|---|---|---|
| `enabled` | bool | `false` | Master switch |
| `provider` | string | `"lmstudio"` | One of `lmstudio`, `openai`, `anthropic`, `openai_compat`. Unknown values fall back to `lmstudio`. |
| `baseUrl` | string | `http://192.168.5.250:1234` | Endpoint root, no trailing slash. For cloud providers, may be left blank — falls back to canonical URL (`https://api.openai.com`, `https://api.anthropic.com`). |
| `model` | string | `""` | Must match a model id returned by `/api/ai/models`. Validated before each request. For OpenAI the discovery endpoint returns ~50 models; non-chat models (embeddings, TTS, moderation, image) are filtered out automatically. |
| `apiKey` | string | `""` | LM Studio normally needs none. Required for `openai` and `anthropic`. |
| `systemPrompt` | string | `""` | Global system prompt override. Empty = built-in default per mode. |
| `temperature` | float | `0.2` | Clamped `[0.0, 2.0]`. Skipped automatically for OpenAI o1/o3/o4 (they only accept default). |
| `maxTokens` | int | `8192` | Clamped `[64, 16384]`. Sent as `max_tokens` for most models, automatically converted to `max_completion_tokens` for OpenAI o1/o3/o4/gpt-5. |
| `timeoutSec` | int | `180` | Clamped `[30, 600]`. HTTP timeout for the inference call. Cloud providers usually finish in 5–30s; local 7B models can take 2–5 min on CPU. |
| `disableThinking` | bool | `true` | Appends ` /no_think` to the system prompt. **Only sent to LM Studio** — ignored for OpenAI / Anthropic / openai_compat to avoid prompt pollution. |
| `tlsInsecure` | bool | `true` | Skip TLS cert verification on HTTPS providers. Default `true` because the API key is the real auth and CA cert rotation on flash is a maintenance nightmare. Set `false` if you've pinned a CA cert. |
| `modes[]` | array | 5 built-ins | See below |

### Modes

Each mode is a small object stored in `ai.modes[]`:

```json
{
  "id": "can_make_now",
  "label": "Can Make Now",
  "instruction": "List cocktails/drinks that can be made immediately from current inventory only. Short list with reasoning.",
  "systemPrompt": "",
  "restrictToInventory": true
}
```

| Field | Purpose |
|---|---|
| `id` | Machine name. Sent in `POST /api/ai/ask` as `mode`. |
| `label` | Shown in the UI dropdown |
| `instruction` | Injected into the user prompt under "Instruction:" |
| `systemPrompt` | Optional per-mode override. Empty falls back to global `aiSystemPrompt`, then to a built-in default that depends on `restrictToInventory`. |
| `restrictToInventory` | If `true`, system prompt tells the model to answer **only** from the inventory snapshot. If `false`, inventory is offered as optional context. |

Built-in defaults (seeded if `modes` is empty):

| id | Restricted | Purpose |
|---|---|---|
| `general` | no | Free-form bartender Q&A; recipes, education, pairing |
| `can_make_now` | yes | What can I drink right now from what I own |
| `missing_ingredients` | yes | What's one bottle away from being possible |
| `recommend_purchases` | yes | Best ROI buys to unlock the most cocktails |
| `shopping_list` | yes | Practical grocery / liquor-store list |

### Inventory serialization

When `includeInventory=true`, the server builds a compact pipe-delimited block in PSRAM and prepends a `BY TYPE` summary. Format per item:

```
type|brand|name|sizeMl|abv|qty|remain%|needBuy|hints|tags
```

This is ~70 chars/item. The default cap (`AI_INVENTORY_CHAR_LIMIT = 96 KB`) fits roughly **1,400 items** before truncation. If truncated, the user prompt explicitly tells the model so the answer can flag the limitation.

A **signature** (cheap hash of inventory state) gates a PSRAM cache — if nothing changed since last request, the serialized block is reused. Cache hits are reflected in `inventoryCacheHit` in the status response.

### `/no_think` and reasoning models

Models like Qwen2.5 and DeepSeek-R1 emit visible chain-of-thought (`<think>…</think>`) by default. That eats your token budget and slows responses. Setting `disableThinking: true` (the default) appends ` /no_think` to the system prompt, which most reasoning models honor. Leave it on unless you specifically want to see the model reason out loud.

### AI REST endpoints

#### `GET /api/ai/models`

Discover models from LM Studio. Tries `/v1/models`, `/api/v1/models`, and `/api/models` in order. Cached for 30 s.

```bash
curl -u admin:admin http://libationlocker.local/api/ai/models
```

```json
{
  "ok": true,
  "models": [
    { "id": "qwen2.5-7b-instruct", "object": "model", "ownedByLmStudio": true },
    { "id": "llama-3.1-8b-instruct", "object": "model", "ownedByLmStudio": true }
  ]
}
```

On failure: `{ "ok": false, "error": "..." }`.

#### `POST /api/ai/test`

Starts a "test" job that asks the configured model to reply with a known string. Validates connectivity, model availability, and basic round-trip.

```bash
curl -u admin:admin -X POST http://libationlocker.local/api/ai/test
```

```json
{ "ok": true, "started": true, "token": "1234567-a1b2c3" }
```

If a job is already running: `409 { "ok": false, "error": "An AI request is already running.", "token": "<existing>" }`.

#### `POST /api/ai/ask`

Starts an "ask" job.

Body:

```json
{
  "question": "What can I make tonight?",
  "includeInventory": true,
  "mode": "can_make_now"
}
```

| Field | Type | Default | Notes |
|---|---|---|---|
| `question` | string | required | Trimmed; empty = `400 missing_question` |
| `includeInventory` | bool | `true` | When false, inventory snapshot is omitted entirely |
| `mode` | string | `general` | Must match a mode `id` (case-insensitive); falls back to first mode if unknown |

Response: `202 { "ok": true, "started": true, "token": "..." }`.

#### `GET /api/ai/status`

Poll for job result. Always returns 200; check `busy` / `done` / `ok`.

```bash
curl -u admin:admin http://libationlocker.local/api/ai/status
```

```json
{
  "busy": false,
  "done": true,
  "ok": true,
  "token": "1234567-a1b2c3",
  "kind": "ask",
  "inventoryIncluded": true,
  "inventoryChars": 14820,
  "inventoryTruncated": false,
  "inventoryCacheHit": true,
  "inventoryItemCount": 217,
  "inventoryTotalItems": 217,
  "modelValidated": true,
  "modeUsed": "can_make_now",
  "httpCode": 200,
  "startedAt": 12345678,
  "finishedAt": 12389012,
  "answer": "Based on your inventory, you can make:\n- Old Fashioned (Maker's Mark...)..."
}
```

On failure, `ok=false` and `error` is set instead of `answer`.

### Recommended polling pattern (client side)

```javascript
async function ask(question, mode) {
  const start = await fetch('/api/ai/ask', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({ question, mode, includeInventory: true })
  }).then(r => r.json());

  if (!start.ok) throw new Error(start.error);
  const myToken = start.token;

  while (true) {
    await new Promise(r => setTimeout(r, 1500));
    const s = await fetch('/api/ai/status').then(r => r.json());
    if (s.token !== myToken) throw new Error('Job was replaced');
    if (s.done) {
      if (!s.ok) throw new Error(s.error);
      return s.answer;
    }
  }
}
```

The bundled web UI implements this pattern with a fullscreen busy overlay and elapsed-time counter.

### Memory model — why this is safe

| Buffer | Backing | Why |
|---|---|---|
| Inventory serialization | PSRAM (`PsramString`) | Up to 96 KB |
| User prompt assembly | PSRAM (`PsramString`) | Question + inventory + instructions |
| Outgoing JSON payload | PSRAM (`PsramString`) | Manually built to skip a second copy through ArduinoJson |
| LM Studio response parse | PSRAM (`PsramJsonDocument`, 192 KB) | 4096-token responses can exceed 100 KB serialized |
| Inventory vector backing | PSRAM (`PsramStdAllocator`) | Keeps ~10–15 KB off internal heap |
| Item `String` fields | Internal heap | Arduino `String` uses `malloc`; this is by design |

The internal heap stays well above the 30 KB warning line during inference on N16R8, even with several hundred items.

---

## Persistence Details

- Inventory and config are persisted to LittleFS under `/data/`
- Writes use a **temp file + rename** strategy (safer against power loss mid-write)
- Saves are coalesced through a background FreeRTOS task — rapid edits don't thrash flash
- If a save fails, the API reports a meaningful error in `{"error":"..."}` and `InventoryStore::lastError()` exposes the last persistence failure
- Optimistic concurrency on item update: client sends `version`; server returns `409 version_conflict` with the current item if it doesn't match

---

## Troubleshooting

### LittleFS mount fails / import says "mount failed"

Symptoms:

- Serial: `esp_littlefs: partition "spiffs" could not be found`
- UI: `Import failed: LittleFS mount failed...`

Fix:

- Ensure your partition table includes a filesystem partition labeled **`spiffs`** with subtype **littlefs** (this repo's `partitions.csv` does)
- Verify Arduino IDE **Flash Size = 16MB** (for ESP32-S3 N16R8)
- Do a one-time **Tools → Erase Flash → All Flash Contents** after changing partitions
- Hit `GET /api/fs` to verify files exist under `/data/`

### `libationlocker.local` doesn't resolve

- Windows: install Bonjour / iTunes (mDNS responder), or use the IP shown in Serial
- Some guest/VLAN networks block multicast DNS; try the same LAN segment
- macOS / iOS / Linux with Avahi: should work out of the box

### Can't upload / wrong boot mode

- Hold **BOOT** while tapping **RESET**, then upload
- Try a known-good **data** USB cable directly into the host (not a hub)

### AI: `/api/ai/test` returns 500 with "Failed to connect to LM Studio"

- Confirm LM Studio's server is bound to `0.0.0.0`, not `127.0.0.1`
- Confirm `baseUrl` host/port matches the LM Studio server tab
- From a desktop on the same LAN: `curl http://<lmstudio-ip>:1234/v1/models` should return JSON
- Check the ESP32 is on the same subnet (or routable) as the LM Studio host

### AI: "Configured model not found in LM Studio: …"

- LM Studio has no model loaded, or the loaded model id differs from what's saved in config
- Hit `GET /api/ai/models` to see the actual ids; PUT one of them into config

### AI: timeouts on long answers

- Increase `ai.timeoutSec` (max 600). 7B models on CPU can take 2–5 minutes for 8K-token answers
- Reduce `maxTokens` (try 2048)
- Use a smaller / faster quantization in LM Studio

### AI: response is truncated mid-sentence

- `maxTokens` too low — raise it. The clip is enforced both server-side (LM Studio) and on the ESP32 (`AI_RESPONSE_CHAR_LIMIT = 48 KB`)

### AI: visible `<think>` chain-of-thought eating my tokens

- Set `ai.disableThinking: true` (default). Adds ` /no_think` to the system prompt
- Some models ignore the directive; switch model or use a non-reasoning variant

### Storage shows `NaN` free

- UI/firmware version skew — current firmware returns `totalBytes / usedBytes / freeBytes`. Reflash with the bundled `WebUiAssets.h`.

### Heap warnings in serial

```
[HEAP] WARNING free=28912 maxAlloc=20480 minEver=24500
[HEAP] CRITICAL free=12000 maxAlloc=8192 minEver=10500 items=987
```

- Inventory size is approaching the practical limit on internal heap
- Reduce inventory, or confirm PSRAM is actually enabled (`[BOOT] PSRAM total=…` in serial)
- An ESP32-S3 with no PSRAM cannot run the AI side — disable it (`ai.enabled: false`)

---

## Repo Layout

| File | Purpose |
|---|---|
| `LibationLocker.ino` | Arduino entry: `setup()`, `loop()`, heap watchdog logging |
| `AppConfig.{h,cpp}` | Compile-time runtime config (AP creds, paths, default Basic Auth) |
| `AppSerial.h` | Serial wrapper macros |
| `AppWiFi.{h,cpp}` | AP-always-on, optional STA join, mDNS, hostname |
| `Models.h` | `Item`, `DropdownConfig`, `AiMode`, `defaultAiModes()` |
| `InventoryStore.{h,cpp}` | In-memory inventory, LittleFS persistence, import/export, mutex |
| `PsramAllocator.h` | `PsramStdAllocator<T>`, `PsramJsonDocument` |
| `WebServerLL.{h,cpp}` | All HTTP routes, AI job task, async body collection |
| `AiAssistant.{h,cpp}` | LM Studio client, prompt assembly, model validation, caches |
| `WebUiAssets.h` | Embedded HTML / CSS / JS for the single-page UI |
| `Favicon.h` | Embedded favicon |
| `partitions.csv` | 16MB partition table with large LittleFS region |

---

## Contributing

PRs welcome. Please:

- Test on real hardware (S3 + LittleFS + PSRAM) before submitting
- Keep changes small and focused
- Match the existing function-based / threading-friendly style — avoid deep class hierarchies
- Don't add cloud / SaaS dependencies. Local-first is a feature.

---

## License

GPL-3.0 — see source headers.

— Dan Roberts
