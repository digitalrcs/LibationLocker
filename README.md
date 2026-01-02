# Libation Locker by Dan Roberts (ESP32-S3)

LibationLocker is a **local, self-hosted inventory web app** for tracking spirits / bottles. It runs entirely on an ESP32 and exposes:
- a single-page web UI (served from flash)
- a small, JSON-based REST API
- persistence via **LittleFS** (inventory + dropdown config)

## Features
- **AP-first** operation (device always creates its own Wi-Fi AP)
- Optional **STA join** to your home Wi-Fi (AP+STA)
- mDNS: browse to **`http://libationlocker.local/`** when connected to your LAN
- Inventory CRUD (type/brand/name/size/ABV/qty/remaining/need/rating/tags/notes)
- Import/Export (JSON/CSV/TXT)
- **Shopping list extract**: export only items where `needToBuy=true`
- LittleFS persistence:
  - `/data/inventory.json`
  - `/data/config.json`

---

## Hardware / Build Notes

### Recommended boards
- ESP32-S3 with PSRAM (ex: **N16R8 = 16MB flash / 8MB PSRAM**)

### Arduino IDE settings (typical)
- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- PSRAM: **Enabled** (OPI if available)
- Erase Flash (first flash after changing partitions): **All Flash Contents**

### Partition table (16MB + LittleFS)
This repo includes a `partitions.csv` that provides a large LittleFS partition.

> Important: the filesystem partition is labeled **`spiffs`** (subtype is still **littlefs**).  
> This matches Arduino-ESP32 defaults so `LittleFS.begin()` mounts cleanly.


## Wiki (GitHub)
This repo ships with pre-written Wiki pages under `wiki/` that you can copy into GitHub Wiki:
- **Home**
- **Installation**
- **API**
- **Architecture**
- **Troubleshooting**
- **Development & Contributing**

See `wiki/README.md` for copy/paste instructions.


---

## Networking

### Access methods
- **AP mode** (always on): connect to the device AP and browse to:
  - `http://192.168.4.1/`
- **STA mode** (optional): configure via API/UI and browse to:
  - `http://libationlocker.local/` (mDNS)
  - or the IP shown in `/api/net`

### Configure STA join
Use `PUT /api/net` with JSON body:
```json
{
  "enabled": true,
  "ssid": "YourWifi",
  "pass": "YourPassword"
}
```

---

## API Reference

Base URL:
- AP mode: `http://192.168.4.1`
- STA mode: `http://libationlocker.local` (or IP from `/api/net`)

All responses are JSON unless noted.

### Health
#### `GET /api/health`
Returns simple OK.
- Response: `{"ok":true}`

Example:
```bash
curl http://192.168.4.1/api/health
```

---

### Network status + config
#### `GET /api/net`
Returns AP + STA status and stored STA config (SSID only).

Response (shape):
```json
{
  "mode":"AP+optional STA",
  "ap": { "ssid":"LibationLocker-xxxx", "ip":"192.168.4.1" },
  "sta": { "connected":false, "ssid":"", "ip":"", "rssi":0 },
  "cfg": { "enabled":false, "ssid":"" }
}
```

#### `PUT /api/net`
Sets stored STA configuration.

Body:
```json
{ "enabled": true, "ssid": "YourWifi", "pass": "YourPassword" }
```

Response: `{"ok":true}`

Example:
```bash
curl -X PUT http://192.168.4.1/api/net \
  -H "Content-Type: application/json" \
  -d '{"enabled":true,"ssid":"YourWifi","pass":"YourPassword"}'
```

---

### Storage / capacity
#### `GET /api/storage`
Returns heap + filesystem usage, and a rough “items left” estimate based on average JSON size.

Key fields:
- `heapTotalBytes`, `heapUsedBytes`, `heapFreeBytes`
- `totalBytes`, `usedBytes`, `freeBytes` (LittleFS)
- `fsMounted` (boolean)
- `avgBytesPerItem`, `estItemsLeft`
- `lastError` (last persistence error string)

Example:
```bash
curl http://192.168.4.1/api/storage
```

---

### Items
#### Item JSON schema
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
  "tags": ["oak","vanilla"],
  "notes": "good neat",
  "updatedAt": 1730000000,
  "version": 3
}
```

Notes:
- `abv` is optional; omitted when unknown.
- `version` is used for **optimistic concurrency** on update.

#### `GET /api/items`
Returns an array of all items:
- Response: `[ {item}, {item}, ... ]`

Example:
```bash
curl http://192.168.4.1/api/items
```

#### `POST /api/item`
Creates an item.
- Required fields: `type`, `brand`, `name`
- `id` provided in the body is ignored; the server generates a new id.
- Response: the created item JSON (with id/version/updatedAt)

Example:
```bash
curl -X POST http://192.168.4.1/api/item \
  -H "Content-Type: application/json" \
  -d '{"type":"Bourbon","brand":"Maker\u0027s Mark","name":"46","sizeMl":750,"abv":45.0,"qty":1,"remainingPct":100,"needToBuy":false,"rating":0,"tags":[],"notes":""}'
```

#### `PUT /api/item`
Updates an existing item (by `id` in body).

- Requires `id`
- Uses optimistic concurrency:
  - If `in.version` does not match stored version, server returns **409**
  - Response includes `current` item so client can resolve conflict

Responses:
- `200` -> updated item JSON
- `404` -> `{"error":"not_found"}`
- `409` -> `{"error":"version_conflict","current":{...}}`

Example:
```bash
curl -X PUT http://192.168.4.1/api/item \
  -H "Content-Type: application/json" \
  -d '{"id":"abc123","type":"Bourbon","brand":"Maker\u0027s Mark","name":"46","sizeMl":750,"abv":45.0,"qty":1,"remainingPct":60,"needToBuy":true,"rating":4,"tags":["oak"],"notes":"re-buy","updatedAt":0,"version":3}'
```

#### `DELETE /api/item?id=<id>`
Deletes an item by id.
- Response: `{"ok":true}`

Example:
```bash
curl -X DELETE "http://192.168.4.1/api/item?id=abc123"
```

---

### Dropdown configuration
Used by the UI for type/size/ABV presets, etc.

#### `GET /api/config`
Returns:
```json
{
  "types":["Bourbon","Rye"],
  "sizesMl":[50,375,750,1000],
  "abvPresets":[40,45,50],
  "remainingPresets":[100,75,50,25,0]
}
```

#### `PUT /api/config`
Body is the same shape as above.
- Response: `{"ok":true}`

Example:
```bash
curl -X PUT http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{"types":["Bourbon","Rye"],"sizesMl":[375,750,1000],"abvPresets":[40,45,50],"remainingPresets":[100,75,50,25,0]}'
```

---

### Filesystem listing (debug)
#### `GET /api/fs`
Returns directory entries for `/` and `/data`.

Response:
```json
{
  "mounted": true,
  "totalBytes": 10380928,
  "usedBytes": 8192,
  "freeBytes": 10372736,
  "entries": [
    { "name": "/data/inventory.json", "size": 12345, "isDir": false }
  ]
}
```

---

### Export
#### `GET /api/export`
Query params:
- `format` = `json` (default), `csv`, `txt`
- `filter` = `need` | `shopping` | `needtobuy` (exports only items where `needToBuy=true`)

Responses are served as downloads using `Content-Disposition: attachment`.

Examples:

**Full JSON bundle (config + items):**
```bash
curl -L "http://192.168.4.1/api/export?format=json" -o libationlocker-export.json
```

**Shopping list (Need=YES) as TXT:**
```bash
curl -L "http://192.168.4.1/api/export?format=txt&filter=need" -o shopping-list.txt
```

**Shopping list as CSV:**
```bash
curl -L "http://192.168.4.1/api/export?format=csv&filter=need" -o shopping-list.csv
```

---

### Import
#### `POST /api/import`
Imports JSON exports (the bundle format produced by `/api/export?format=json`).

Query params:
- `mode`:
  - `merge` (default): update by id when present; add when missing
  - `append`: always add new items; if ids collide, imported items get new ids
  - `replace`: replaces entire inventory with incoming items
- `dryrun` = `1|true|yes` (optional): computes counts but does not persist

Body: JSON bundle containing at least:
```json
{ "items": [ {item}, ... ], "config": { ... } }
```

Response:
```json
{ "ok": true, "mode":"merge", "dryRun": false, "addCount": 3, "updateCount": 2, "conflictCount": 0 }
```

Example (merge):
```bash
curl -X POST "http://192.168.4.1/api/import?mode=merge" \
  -H "Content-Type: application/json" \
  --data-binary "@libationlocker-export.json"
```

Example (dry run replace):
```bash
curl -X POST "http://192.168.4.1/api/import?mode=replace&dryrun=1" \
  -H "Content-Type: application/json" \
  --data-binary "@libationlocker-export.json"
```

---


## Sample export file
Minimal valid `libationlocker-export.json` (config + items) you can import via `/api/import`:

```json
{
  "app": "LibationLocker",
  "version": 1,
  "config": {
    "types": ["Bourbon", "Rye", "Gin", "Tequila"],
    "sizesMl": [375, 750, 1000, 1750],
    "abvPresets": [40, 45, 50],
    "remainingPresets": [100, 75, 50, 25, 0]
  },
  "items": [
    {
      "id": 1,
      "type": "Bourbon",
      "brand": "Maker's Mark",
      "name": "Kentucky Straight Bourbon",
      "sizeMl": 750,
      "abv": 45.0,
      "qty": 0,
      "remainingPct": 0,
      "needToBuy": true,
      "rating": 4,
      "tags": ["daily", "cocktails"],
      "notes": "Good all-rounder.",
      "updatedAt": 1700000000,
      "version": 1
    }
  ]
}
```

Notes:
- `id` is the item id. If you omit it during import, the device will assign one.
- `updatedAt` is an epoch timestamp (seconds). If omitted, the device sets it.
- `version` is used for optimistic concurrency (`PUT /api/item`).


## Persistence details
- Inventory and config are persisted to LittleFS under `/data/`.
- Writes use a temp file + rename (safer against power loss mid-write).
- If a save fails, the API reports a meaningful error in `{"error":"..."}` and `InventoryStore::lastError()`.

---

## Security notes
- This is intended for **trusted local networks**. There is **no authentication**.
- If you enable STA mode, the API/UI are reachable on your LAN.

---


## Troubleshooting

### LittleFS mount fails / import says “mount failed”
Symptoms:
- Serial: `esp_littlefs: partition "spiffs" could not be found`
- UI: `Import failed: LittleFS mount failed...`

Fix:
- Ensure your partition table includes a filesystem partition labeled **`spiffs`** with subtype **littlefs** (this repo's `partitions.csv` does).
- Verify Arduino IDE **Flash Size = 16MB** (for ESP32-S3 N16R8).
- Do a one-time **Tools → Erase Flash → All Flash Contents** after changing partitions.
- Hit `GET /api/fs` to verify files exist under `/data/`.

### Storage shows `NaN` free
This happens if the UI expects `freeBytes` but the API returns a different field name.  
Use the current firmware (it returns `totalBytes/usedBytes/freeBytes`).

### Import says “failed” but items appear until reboot
That means RAM updated but persistence failed.  
Use `GET /api/fs` to confirm `/data/inventory.json` exists and grows after import.

### `libationlocker.local` doesn't resolve
- Windows: install Bonjour / iTunes (mDNS responder), or use the IP shown in Serial.
- Some guest/VLAN networks block multicast DNS; try same LAN segment.

### Can't upload / wrong boot mode
- Hold **BOOT** while tapping **RESET**, then upload.
- If using a USB hub/cable, try a known-good data cable and direct port.


## Repo layout
- `LibationLocker.ino`: app bootstrap
- `WebServerLL.*`: HTTP routes + UI delivery
- `WebUiAssets.h`: embedded HTML/CSS/JS
- `InventoryStore.*`: in-memory model + LittleFS persistence + import/export
- `AppWiFi.*`: AP + optional STA join, DHCP hostname, mDNS
- `partitions.csv`: 16MB partition table with large LittleFS

---

## Contributing
PRs welcome. Keep changes small and test on real hardware (S3 + LittleFS) before submitting.
