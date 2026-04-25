# Architecture

## Overview

```
                          Browser
                             │
                  HTTP + Basic Auth
                             │
   ┌─────────────────────────▼─────────────────────────┐
   │                    ESP32-S3                       │
   │                                                   │
   │   ┌──────────────────────────────────────────┐    │
   │   │  AsyncTCP task  (must never block)       │    │
   │   │   ESPAsyncWebServer routes (WebServerLL) │    │
   │   └────┬───────────────┬─────────────────────┘    │
   │        │               │                          │
   │        │               │ start AI job             │
   │        │               ▼                          │
   │        │     ┌─────────────────────┐              │
   │        │     │  ai_job task        │              │
   │        │     │  core 1, 32 KB stk  │              │
   │        │     │  AiAssistant::ask() │──HTTP─────►──┼─► LM Studio (LAN)
   │        │     └─────────────────────┘              │   /v1/chat/completions
   │        │                                          │
   │        ▼                                          │
   │   ┌──────────────────────────────────┐            │
   │   │  InventoryStore                  │            │
   │   │   ItemVector in PSRAM            │            │
   │   │   SemaphoreHandle_t _lock        │            │
   │   │   inventorySaveTask (background) │            │
   │   └──────────────────┬───────────────┘            │
   │                      │                            │
   │                      ▼                            │
   │              ┌──────────────┐                     │
   │              │   LittleFS   │                     │
   │              │  /data/*.json│                     │
   │              └──────────────┘                     │
   └───────────────────────────────────────────────────┘
```

## Components

### `LibationLocker.ino`

`setup()` boots: `AppConfig` → `InventoryStore` → `AppWiFi` → `WebServerLL`.

`loop()` does two things: pump `AppWiFi::loop()` (mostly mDNS / STA reconnect), and a periodic heap-health logger. Thresholds:

- WARN at 30 KB free heap
- CRITICAL at 15 KB free heap (logged immediately, throttled to once per 5 s)
- Otherwise log every 30 s if at warning level

### `AppWiFi`

AP is always on. STA is gated by `cfg.enabled` in the persisted config (separate from `AppConfig` runtime config — STA config lives in `InventoryStore`'s config doc indirectly via `/api/net`). mDNS responder publishes `libationlocker.local`.

### `InventoryStore`

| Concern | How |
|---|---|
| In-memory vector | `ItemVector = std::vector<Item, PsramStdAllocator<Item>>` — backing array in PSRAM |
| Locking | Single `SemaphoreHandle_t _lock` mutex around all reads + writes |
| Read iteration | `forEach(cb)` holds the lock; callback returns false to stop early. Avoids the cost of full `getAll()` copies. |
| Persistence | Background task `inventorySaveTask` — coalesces writes, temp-file + rename |
| Streaming export | `streamExportJson(Print&)` writes JSON directly into `AsyncResponseStream` |
| Optimistic concurrency | `update()` checks incoming `version` against stored, sets `versionConflict=true` and returns `current` if mismatch |

### `WebServerLL`

`ESPAsyncWebServer` on port 80. Every route calls `requireAuth()` first. JSON bodies arrive in chunks; `handleBodyCollect_()` accumulates into a heap `String*` stashed in `_tempObject`, then dispatches the completion lambda once `index + len == total`.

### `AiAssistant`

Stateless from the outside. Internally caches:

- **Inventory serialization cache** — `PsramString s_inventoryCache` keyed by a 32-bit `inventorySignature()`. Skip re-serialization when nothing changed.
- **Model list cache** — `s_modelCache`, 30-second TTL.

All large buffers are PSRAM-backed. The user prompt is built directly into a `PsramString`, then JSON-escaped into another `PsramString` payload buffer — never into Arduino `String` (which is internal-heap).

### `AiJobState` (in `WebServerLL.cpp`)

| Field | Purpose |
|---|---|
| `busy` | True while a job is running |
| `done` | True once the job finishes (success or failure) |
| `ok` | Result flag |
| `token` | Unique id; clients verify their job wasn't replaced |
| `kind` | `"ask"` or `"test"` |
| `answer` / `error` | Result payload |
| `inventory*` | Diagnostic fields (chars serialized, items, truncation, cache hit) |
| `httpCode` | Underlying LM Studio HTTP status |

A `SemaphoreHandle_t g_aiJobMutex` protects every read/write of the singleton `g_aiJob`. Only one job runs at a time. A second `POST /api/ai/ask` while busy returns `409` with the existing token — the client can either wait on `/api/ai/status` for the current job or surface the conflict.

## Why one job at a time

- LM Studio inference uses ~all the CPU on the host; queuing two requests gains nothing
- A second outbound HTTP connection from the ESP32 would compete for the small number of TCP sockets and the network stack tasks
- The single-job invariant is enforced by mutex + flag, not a queue, which keeps the state machine trivial to reason about

## Memory budget (N16R8)

| Pool | Approx | Used by |
|---|---|---|
| Internal SRAM | ~320 KB free at boot | AsyncTCP, WiFi, LWIP, FreeRTOS, item String fields |
| PSRAM | 8 MB | Inventory vector backing, prompt buffers, response parse doc, inventory cache |
| LittleFS | ~10 MB | `/data/inventory.json`, `/data/config.json` |

The internal heap is the constrained resource. Every large transient buffer in the AI path is deliberately PSRAM-backed for that reason.

## Threading model

| Task | Core | Stack | Role |
|---|---|---|---|
| `loopTask` | 1 | default | `setup()` / `loop()` |
| AsyncTCP | 0 | default | TCP / web server callbacks (must never block) |
| `inventory_save` | (default) | 8 KB | Background flushes |
| `ai_job` | 1 | 32 KB | LM Studio HTTP call + response parse |

## Why `/no_think`

Reasoning models (Qwen2.5, DeepSeek-R1) emit `<think>…</think>` blocks by default that consume tokens before the actual answer. On a CPU-bound LM Studio host this can mean a 60-second answer becomes a 4-minute answer.

`aiDisableThinking: true` (default) appends ` /no_think` to the system prompt. Most reasoning models respect the directive. For non-reasoning models it's a harmless no-op.

## HTTPClient timeout workaround

`HTTPClient::setTimeout()` truncates to `uint16_t` in many ESP32 core versions. So `555 * 1000 ms = 555000 → 30712 ms`. For inference timeouts of 60–600 seconds this is a real bug.

Workaround in `AiAssistant::ask`: pre-connect a `WiFiClient` directly with the desired timeout, then hand it to `HTTPClient::begin(client, endpoint)`. When `HTTPClient::POST()` calls its internal `connect()`, it sees the socket is already open and skips the timeout override.
