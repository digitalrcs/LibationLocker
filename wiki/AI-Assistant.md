# AI Assistant

The AI assistant is an **optional**, **local-only** feature. It connects to an [LM Studio](https://lmstudio.ai) instance on your LAN and uses your live inventory as grounding context. No data leaves your network.

## Why local

- Privacy: your liquor cabinet inventory and prompts never leave your house
- Latency: no round-trip to a public API
- Cost: zero per-request
- Offline: works without internet, as long as the ESP32 and LM Studio host are on the same LAN

## LM Studio setup (one-time)

1. Install **LM Studio** (Windows / macOS / Linux) on a machine on the same LAN as the ESP32. A laptop, NAS, or mini-PC works fine.
2. Download a model. Reasonable starting points:
   - **Qwen2.5-7B-Instruct** — fast, good follow-the-instruction behavior, supports `/no_think`
   - **Llama-3.1-8B-Instruct** — solid general performance
   - Quantization: Q4_K_M is a good speed/quality tradeoff for 7–8B on CPU
3. Open LM Studio → **Server** tab.
4. **Important**: bind the server to `0.0.0.0`, not `127.0.0.1`, so the ESP32 can reach it. Default port `1234`.
5. Click **Start Server**.
6. Verify from another machine on the LAN: `curl http://<host-ip>:1234/v1/models` should return JSON.

## ESP32 configuration

Either through the web UI's settings panel, or via the API:

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

### Configuration fields

| Field | Type | Default | Notes |
|---|---|---|---|
| `enabled` | bool | `false` | Master switch |
| `baseUrl` | string | `http://192.168.5.250:1234` | LM Studio root, no trailing slash |
| `model` | string | `""` | Must match a model id from `/api/ai/models` |
| `apiKey` | string | `""` | LM Studio normally needs none; sent as `Bearer` if set |
| `systemPrompt` | string | `""` | Global override; empty = built-in default |
| `temperature` | float | `0.2` | Clamped `[0.0, 2.0]` |
| `maxTokens` | int | `8192` | Clamped `[64, 16384]` |
| `timeoutSec` | int | `180` | Clamped `[30, 600]` |
| `disableThinking` | bool | `true` | Appends ` /no_think` to system prompt |
| `modes` | array | 5 built-ins | See [Modes](#modes) below |

## Verify it works

1. Discover models:

```bash
curl -u admin:admin http://libationlocker.local/api/ai/models
```

   Confirm the id you put in `ai.model` is in the list.

2. Run the round-trip test:

```bash
curl -u admin:admin -X POST http://libationlocker.local/api/ai/test
# {"ok":true,"started":true,"token":"..."}

curl -u admin:admin http://libationlocker.local/api/ai/status
# busy=true, then done=true, ok=true, answer="LibationLocker AI connection OK."
```

## Modes

A mode is a small object that shapes how the AI is asked:

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
| `label` | Human-readable; shown in UI dropdown |
| `instruction` | Injected into the user prompt under "Instruction:" |
| `systemPrompt` | Optional per-mode override. Empty = global `aiSystemPrompt` → built-in default. |
| `restrictToInventory` | If `true`, system prompt forces "answer using ONLY the provided inventory" |

### Built-in modes (seeded if `modes` is empty)

| id | Restricted | Purpose |
|---|---|---|
| `general` | no | Free-form bartender Q&A; recipes, education, pairings |
| `can_make_now` | yes | What can I drink right now from what I own |
| `missing_ingredients` | yes | What's one bottle away from being possible |
| `recommend_purchases` | yes | Best ROI buys to unlock the most cocktails |
| `shopping_list` | yes | Practical grocery / liquor-store list |

### Adding your own mode

PUT a config that includes your new mode in `ai.modes`:

```json
{
  "id": "halloween",
  "label": "Halloween Cocktails",
  "instruction": "Suggest spooky / themed cocktails appropriate for Halloween. Use the inventory where possible.",
  "systemPrompt": "",
  "restrictToInventory": false
}
```

The UI will pick it up on next config load.

## How a request is built

1. Client `POST /api/ai/ask` with `{ question, includeInventory, mode }` → returns `202 + token`
2. The `ai_job` task (FreeRTOS, core 1):
   1. Validates the configured model is loaded (`GET /v1/models`, cached 30 s)
   2. Builds a compact pipe-delimited inventory snapshot in PSRAM (cached on inventory signature)
   3. Resolves the mode → instruction + system prompt + restriction flag
   4. Assembles the user prompt: `Task mode: ... \nInstruction: ... \n\nUser question: ...\n\n<inventory>\n<restriction note>`
   5. Builds the system prompt — per-mode override → global → built-in default. Appends ` /no_think` if `disableThinking`.
   6. JSON-escapes both directly into a PSRAM payload buffer (no double-copy through ArduinoJson)
   7. Pre-connects a `WiFiClient` (works around `HTTPClient::setTimeout()` `uint16_t` truncation)
   8. `POST /v1/chat/completions` with `Authorization: Bearer <apiKey>` if set
   9. Parses response into a 192 KB `PsramJsonDocument`, extracts `choices[0].message.content`
   10. Updates `g_aiJob` under mutex; client sees `done=true` next poll
3. Client polls `GET /api/ai/status` every 1–2 seconds until `done=true`

## Inventory format the model sees

```
LibationLocker inventory (217 items)
BY TYPE: Bourbon: 12, Rye: 4, Gin: 7, Tequila: 5, Vermouth: 2, ...

ITEMS (pipe-delimited: type|brand|name|sizeMl|abv|qty|remain%|needBuy|hints|tags):
Bourbon|Maker's Mark|46|750|45.0|1|60|0|caramel,oak|oak,vanilla
Bourbon|Buffalo Trace|Standard|750|45.0|1|80|0|caramel,vanilla|daily
Rye|Rittenhouse|Bottled-in-Bond|750|50.0|1|30|1|rye,spice|cocktails
...
```

A `BY TYPE` summary leads so the model has a quick mental map. Items are then dumped pipe-delimited (~70 chars/item). The default cap (`AI_INVENTORY_CHAR_LIMIT = 96 KB`) fits roughly **1,400 items** before truncation. If truncated, the prompt explicitly tells the model to flag the limitation in its answer.

## Tuning

### "Answers are slow"

7B models on CPU run roughly 5–15 tokens/sec. An 8K-token answer can take 8 minutes worst case. Either:

- Lower `maxTokens` to 2048 (most cocktail answers fit easily)
- Use a smaller / more aggressively quantized model
- Run LM Studio on a machine with a GPU

### "Answers ramble"

Lower `temperature` (try 0.1) for crisp factual answers. Raise (0.7) for more creative cocktail riffs.

### "Answers include `<think>…</think>` blocks"

Make sure `disableThinking: true` (default). Some models ignore the directive — switch model.

### "Answers wander off-inventory in restricted modes"

Strengthen the per-mode `systemPrompt` override. Example:

```json
"systemPrompt": "You are a bartender. The user has provided their EXACT inventory. Do not suggest any cocktail that requires an ingredient not on the list. If a cocktail is impossible, do not list it. Cite the specific item lines you used for each suggestion."
```

### "Connection works, but `Configured model not found`"

The string in `ai.model` must exactly match an `id` from `GET /api/ai/models`. LM Studio model ids change when you re-download or re-import a model.

## Client polling pattern

```javascript
async function ask(question, mode) {
  const start = await fetch('/api/ai/ask', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ question, mode, includeInventory: true })
  }).then(r => r.json());

  if (!start.ok) throw new Error(start.error);
  const myToken = start.token;

  while (true) {
    await new Promise(r => setTimeout(r, 1500));
    const s = await fetch('/api/ai/status').then(r => r.json());
    if (s.token !== myToken) throw new Error('Job replaced');
    if (s.done) {
      if (!s.ok) throw new Error(s.error);
      return { answer: s.answer, stats: s };
    }
  }
}
```

The bundled UI implements this with a fullscreen busy overlay, elapsed-time counter, and markdown rendering of the response.

## Diagnostic fields in `/api/ai/status`

| Field | Meaning |
|---|---|
| `inventoryChars` | Bytes of inventory text sent to the model |
| `inventoryItemCount` | Items actually serialized (post-truncation) |
| `inventoryTotalItems` | Items in the store |
| `inventoryTruncated` | True if `inventoryItemCount < inventoryTotalItems` |
| `inventoryCacheHit` | True if the PSRAM cache was reused (no re-serialization) |
| `modelValidated` | True if the configured model id was confirmed in `/v1/models` |
| `modeUsed` | Final resolved mode id |
| `httpCode` | LM Studio HTTP status (200 on success) |
| `startedAt` / `finishedAt` | `millis()` timestamps |
