# Installation

## Hardware

**Required:**
- ESP32-S3 with PSRAM. Recommended: **N16R8** (16 MB flash, 8 MB PSRAM).
- USB-C data cable (not charge-only).

PSRAM is mandatory if you want the AI assistant. Without it, the firmware boots and serves the inventory app, but AI requests will exhaust the internal heap.

## Software

- Arduino IDE 2.x (or Arduino CLI)
- ESP32 Arduino core 2.0.14 or newer
- Libraries:
  - `ESPAsyncWebServer` (mathieucarbou or me-no-dev fork)
  - `AsyncTCP`
  - `ArduinoJson` v6.x

`LittleFS`, `HTTPClient`, and `WiFi` ship with the ESP32 core.

## Arduino IDE settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB |
| PSRAM | OPI PSRAM |
| Partition Scheme | Custom (use bundled `partitions.csv`) |
| Core Debug Level | None or Error |
| Upload Speed | 921600 |

## First flash

1. Open `LibationLocker.ino` in Arduino IDE.
2. Select board + port.
3. **Tools → Erase All Flash Before Sketch Upload → Enabled** (one-time, after changing partitions).
4. Upload.
5. Open Serial Monitor at 115200. You should see:

```
[BOOT] Heap total=... free=... maxAlloc=...
[BOOT] PSRAM total=8388608 free=...
=== LibationLocker by Dan ===
[NET] Mode: AP always-on + optional STA
[NET] AP SSID: LibationLocker
[NET] AP IP:   192.168.4.1
[NET] AP PASS: adminadmin
[WEB] Basic Auth user: admin
[WEB] Basic Auth pass: admin
[BOOT] Ready
```

If you see `[BOOT] PSRAM not found`, your board/IDE PSRAM setting is wrong.

## First connect

1. Join Wi-Fi network `LibationLocker` (password `adminadmin`).
2. Browse to `http://192.168.4.1/`.
3. Log in with `admin` / `admin`.
4. **Change the credentials.** Edit `AppConfig.h`, recompile, reflash. The defaults are intentionally weak so first boot just works — they're not safe for any shared environment.

## Optional: join your home Wi-Fi

```bash
curl -u admin:admin -X PUT http://192.168.4.1/api/net \
  -H "Content-Type: application/json" \
  -d '{"enabled":true,"ssid":"YourSSID","pass":"YourPassword"}'
```

After joining, the device is reachable on **both** networks at the same time:
- `http://192.168.4.1/` (its own AP, always)
- `http://libationlocker.local/` (your LAN, via mDNS)
- The IP shown by `GET /api/net` (your LAN, direct)

## Optional: enable the AI assistant

See [AI Assistant](AI-Assistant) for the full setup. Short version:

1. Install LM Studio on a desktop on the same LAN.
2. Load a 7B-class instruct model.
3. Enable LM Studio's local server bound to `0.0.0.0:1234`.
4. PUT the AI config:

```bash
curl -u admin:admin -X PUT http://libationlocker.local/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "types":["Bourbon","Rye","Gin","Tequila"],
    "sizesMl":[375,750,1000,1750],
    "abvPresets":[40,45,50],
    "remainingPresets":[100,75,50,25,0],
    "ai": {
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

5. Test: `curl -u admin:admin -X POST http://libationlocker.local/api/ai/test`, then poll `/api/ai/status`.

## Partition table reference

The bundled `partitions.csv` reserves a large LittleFS partition. Critical detail: the partition is **labeled `spiffs`** with **subtype `littlefs`**. This matches Arduino-ESP32 defaults so `LittleFS.begin()` mounts cleanly without explicit args. Don't rename it.
