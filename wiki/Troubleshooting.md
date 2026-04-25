# Troubleshooting

## Boot / hardware

### `[BOOT] PSRAM not found`

- Arduino IDE: **Tools → PSRAM → OPI PSRAM** (for N16R8)
- Verify your board actually has PSRAM (the N16R8 variant does; plain N16 does not)
- Without PSRAM, the inventory app runs but the AI side will fail; set `ai.enabled: false`

### Won't enter download mode

- Hold **BOOT**, tap **RESET**, release **BOOT**, then upload
- Use a **data** USB cable, plugged directly into the host (no hub)
- On Linux: confirm `/dev/ttyACM0` or `/dev/ttyUSB0` exists and your user is in the `dialout` group

### Boot loop / brownout

- Power supply too weak. ESP32-S3 with PSRAM during Wi-Fi TX peaks well over what some USB ports deliver. Use a powered hub or a real 5V 1A+ source.

## Filesystem

### `esp_littlefs: partition "spiffs" could not be found`

- The bundled `partitions.csv` must be selected. It labels the FS partition `spiffs` (subtype `littlefs`).
- After changing partitions: **Tools → Erase Flash → All Flash Contents**, then upload.
- Verify with `GET /api/fs`.

### `Import failed: LittleFS mount failed`

- Same root cause as above. Reflash with the bundled partitions and erase all flash.

### `GET /api/storage` shows `NaN` or strange free-space numbers

- UI / firmware version skew. Reflash with the bundled `WebUiAssets.h`.

## Network

### `libationlocker.local` doesn't resolve

- Windows without Bonjour: install Apple Bonjour Print Services, or use the IP from `GET /api/net`
- Some guest / VLAN networks block multicast DNS. Connect to the same physical LAN segment.
- Confirm the device actually joined: `[NET] STA IP: ...` in serial.

### STA join fails

- `GET /api/net` should show `sta.connected=false` and `cfg.ssid` set
- Check serial for WPA / auth errors
- Some routers reject ESP32 on certain channels (12–14 in EU, DFS channels). Pin a 2.4 GHz channel ≤ 11 if possible.

### Can reach the device on AP but not over LAN

- The device runs AP+STA simultaneously. After STA join, both interfaces work.
- If only AP works: STA didn't actually associate. Check serial.

## Auth

### 401 on every request

- Default Basic Auth is `admin:admin`. Use `curl -u admin:admin ...` or supply credentials in your client.
- If you changed `webUser`/`webPass` in `AppConfig.h`, recompile and reflash — these are compile-time defaults, not persisted.

### Want to disable auth entirely

- Set `webUser = ""` in `AppConfig.h`, recompile. Auth is then skipped on all routes.

## Inventory

### Imports work but disappear after reboot

- The save failed silently. Check `GET /api/storage` → `lastError`, and `GET /api/fs` → confirm `/data/inventory.json` exists with reasonable size.
- Most common cause: filesystem isn't actually mounted. See "esp_littlefs: partition not found" above.

### `409 version_conflict` on update

- Optimistic concurrency caught a stale write. Refetch the item with `GET /api/items` (or use the `current` field returned in the 409 response), apply your changes on top, and retry the `PUT`.

## AI

### `/api/ai/test` fails with "Failed to connect to LM Studio"

1. Confirm LM Studio's server is bound to **`0.0.0.0`**, not `127.0.0.1`.
2. From a desktop on the same LAN: `curl http://<lmstudio-ip>:1234/v1/models`. Must return JSON.
3. Confirm `ai.baseUrl` matches exactly, including port. No trailing slash.
4. Confirm the ESP32 and LM Studio are on the same subnet, or routable.

### `Configured model not found in LM Studio: <id>`

- LM Studio has no model loaded, or the loaded model's id doesn't match `ai.model`.
- `curl -u admin:admin http://libationlocker.local/api/ai/models` shows what LM Studio is actually serving.
- Update `ai.model` to match exactly.

### Timeouts on long answers

- `ai.timeoutSec` defaults to 180. Raise to 300–600 for slow CPU inference.
- Lower `ai.maxTokens` (try 2048) — most cocktail questions don't need 8K of output.
- Use a smaller / more aggressively quantized model.

### Visible `<think>…</think>` blocks in responses

- `ai.disableThinking: true` (default) appends ` /no_think`. Some reasoning models ignore it.
- Try `Qwen2.5-7B-Instruct` (respects the directive) instead of `DeepSeek-R1-Distill-*` if you don't want reasoning visible.

### Response truncated mid-sentence

- Hit `ai.maxTokens`. Raise it (max 16384).
- Or hit the firmware's response cap (`AI_RESPONSE_CHAR_LIMIT = 48 KB`) — very rare in practice.

### `ai_status_failed` from `/api/ai/status`

- Mutex acquisition timed out. Almost always means another job is mid-finalization. Retry in 200 ms.

### `409 An AI request is already running`

- One job at a time by design. Wait for the current one to finish (poll `/api/ai/status`) or accept the existing token returned in the 409 body.

### Heap warnings during AI requests

```
[HEAP] WARNING free=28912 maxAlloc=20480 minEver=24500
```

- Inventory is large enough that the in-flight String fields are stressing internal heap.
- Confirm PSRAM is actually enabled (look for `[BOOT] PSRAM total=8388608` in serial). If `PSRAM not found`, the AI path is unsafe and should be disabled.
- Reduce inventory size, or split into a separate device.

### AI answers are wrong / hallucinate ingredients you don't own

- Switch to a `restrictToInventory: true` mode (e.g., `can_make_now`).
- Strengthen the per-mode `systemPrompt` to explicitly forbid out-of-inventory suggestions.
- Try a stronger model (8B+). Tiny 1–3B models will hallucinate regardless of prompting.

## Logs

Useful serial filters:

| Prefix | Source |
|---|---|
| `[BOOT]` | Initial boot diagnostics |
| `[NET]` | AP/STA join, mDNS |
| `[WEB]` | Auth state |
| `[HEAP]` | Heap-health watchdog |
| `[AI]` | AI request lifecycle, payload sizes, mode resolution |

Enabling Arduino core debug (`Tools → Core Debug Level → Verbose`) is rarely necessary and produces a lot of noise. Use `Error` or `Warn` if you need more.
