# API Reference

All endpoints require HTTP Basic Auth (default `admin:admin`). All responses are JSON unless noted (export endpoints stream files).

Base URL is one of:
- `http://192.168.4.1` (AP mode)
- `http://libationlocker.local` (mDNS on your LAN)
- The IP returned by `GET /api/net` (direct on your LAN)

## Conventions

- Body for `POST` / `PUT` is `Content-Type: application/json` unless stated otherwise
- Error responses: `{"error":"<short_token>"}` with an HTTP status code
- All write endpoints persist atomically (temp file + rename)

## Health

### `GET /api/health`

```json
{"ok": true}
```

## Network

### `GET /api/net`

```json
{
  "mode": "AP+optional STA",
  "ap":   { "ssid": "LibationLocker", "ip": "192.168.4.1" },
  "sta":  { "connected": false, "ssid": "", "ip": "", "rssi": 0 },
  "cfg":  { "enabled": false, "ssid": "" }
}
```

### `PUT /api/net`

```json
{ "enabled": true, "ssid": "YourSSID", "pass": "YourPassword" }
```

Response: `{"ok": true}`. The device joins immediately.

## Storage

### `GET /api/storage`

```json
{
  "heapTotalBytes": 327680,
  "heapUsedBytes": 91200,
  "heapFreeBytes": 236480,
  "totalBytes": 10380928,
  "usedBytes": 16384,
  "freeBytes": 10364544,
  "fsMounted": true,
  "avgBytesPerItem": 220,
  "estItemsLeft": 47000,
  "lastError": ""
}
```

### `GET /api/fs`

Lists `/` and `/data` for debugging.

## Items

### Schema

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
  "tags": ["oak"],
  "notes": "neat",
  "updatedAt": 1730000000,
  "version": 3
}
```

`abv` may be omitted. `version` drives optimistic concurrency.

### `GET /api/items`

Returns an array of all items.

### `POST /api/item`

Required: `type`, `brand`, `name`. Server generates `id`, `updatedAt`, `version=1`.

```bash
curl -u admin:admin -X POST http://libationlocker.local/api/item \
  -H "Content-Type: application/json" \
  -d '{"type":"Bourbon","brand":"Maker'\''s Mark","name":"46","sizeMl":750,"abv":45,"qty":1,"remainingPct":100}'
```

### `PUT /api/item`

Update by `id` in body. Must include current `version`.

| Code | Meaning |
|---|---|
| 200 | Updated; returns the updated item |
| 400 | `{"error":"missing_id"}` etc. |
| 404 | `{"error":"not_found"}` |
| 409 | `{"error":"version_conflict","current":{...}}` — refetch and retry |

### `DELETE /api/item?id=<id>`

Returns `{"ok": true}`.

## Config (dropdowns + AI)

### `GET /api/config`

```json
{
  "types": ["Bourbon", "Rye", "Gin", "Tequila"],
  "sizesMl": [50, 375, 750, 1000, 1750],
  "abvPresets": [40, 45, 50],
  "remainingPresets": [100, 75, 50, 25, 0],
  "ai": {
    "enabled": false,
    "baseUrl": "http://192.168.5.250:1234",
    "model": "",
    "apiKey": "",
    "systemPrompt": "",
    "temperature": 0.2,
    "maxTokens": 8192,
    "timeoutSec": 180,
    "disableThinking": true,
    "modes": [ /* 5 built-in modes */ ],
    "availableModels": []
  }
}
```

### `PUT /api/config`

Same shape. Server clamps:

- `temperature` to `[0.0, 2.0]`
- `maxTokens` to `[64, 16384]`
- `timeoutSec` to `[30, 600]`

If `modes` is empty/missing, the five built-in defaults are seeded.

## Export

### `GET /api/export?format=<json|csv|txt>&filter=<need>`

Streams a download with `Content-Disposition: attachment`.

| `format` | Output |
|---|---|
| `json` | Full bundle: `{ app, version, config, items }` |
| `csv` | Items only, header row |
| `txt` | Items only, plain text |

| `filter` | Effect |
|---|---|
| (omitted) | All items |
| `need` / `shopping` / `needtobuy` | Only items where `needToBuy=true` |

```bash
curl -u admin:admin -L "http://libationlocker.local/api/export?format=json" -o backup.json
curl -u admin:admin -L "http://libationlocker.local/api/export?format=txt&filter=need" -o shop.txt
```

## Import

### `POST /api/import?mode=<merge|append|replace>&dryrun=<0|1>`

Body: a bundle from `GET /api/export?format=json`.

| `mode` | Behavior |
|---|---|
| `merge` (default) | Update by id where present, add where missing |
| `append` | Always add; if id collides, generate new id |
| `replace` | Replace entire inventory |

`dryrun=1` computes counts without persisting.

Response:

```json
{ "ok": true, "mode": "merge", "dryRun": false,
  "addCount": 3, "updateCount": 2, "conflictCount": 0 }
```

## AI

See [AI Assistant](AI-Assistant) for setup and prompt design. The endpoints:

### `GET /api/ai/models`

Discovers models from LM Studio (tries `/v1/models`, `/api/v1/models`, `/api/models`). 30-second cache.

```json
{
  "ok": true,
  "models": [
    { "id": "qwen2.5-7b-instruct", "object": "model", "ownedByLmStudio": true }
  ]
}
```

### `POST /api/ai/test`

Starts an asynchronous "test" job. Returns immediately with a token.

```json
{ "ok": true, "started": true, "token": "1234567-a1b2c3" }
```

`409` if a job is already running.

### `POST /api/ai/ask`

Starts an asynchronous "ask" job.

```json
{
  "question": "What can I make tonight?",
  "includeInventory": true,
  "mode": "can_make_now"
}
```

Returns `202` with `{ ok, started, token }`, or `409` if a job is already running, or `400 missing_question` for empty input.

### `GET /api/ai/status`

Poll for job state. Always 200.

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
  "answer": "..."
}
```

If the job failed: `ok=false` and `error` populated instead of `answer`.

## Auth

HTTP Basic Auth on every route. Disable by setting `webUser=""` in `AppConfig.h` and recompiling.

If `webUser` is set, the server returns `401 WWW-Authenticate: Basic realm="..."` on missing/wrong credentials.
