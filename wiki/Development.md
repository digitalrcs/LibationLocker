# Development & Contributing

## Code style

- **Function-first.** Avoid deep class hierarchies. The codebase favors free functions and small static methods on a class (e.g., `InventoryStore::create`, `AiAssistant::ask`).
- **Threading-friendly.** Long work runs in a FreeRTOS task. The AsyncTCP task **never** blocks. Body collection uses `_tempObject`-stashed `String*` accumulators, not blocking reads.
- **PSRAM for transient large buffers.** Anything over ~2 KB that's a transient buffer goes in `PsramString` or `PsramJsonDocument`. Internal heap is the constrained resource.
- **Mutex everything that's shared.** `_lock` in `InventoryStore`, `g_aiJobMutex` in `WebServerLL`. Never read/write shared state without the lock.
- **Atomic writes.** Persistence uses temp-file + rename. Don't `truncate` and rewrite.
- **Optimistic concurrency on item updates.** Don't add silent last-writer-wins paths.
- **Least privilege.** Auth on every route. No "internal" endpoints that bypass auth.

## Adding a new REST endpoint

1. In `WebServerLL.cpp`, register the route in `WebServerLL::begin()`.
2. First line of every handler: `if (!requireAuth(req)) return;`.
3. For body endpoints, use `handleBodyCollect_()` to assemble the payload before parsing — `AsyncWebServer` chunks bodies.
4. Parse JSON into the smallest reasonable `DynamicJsonDocument` or, for AI-sized payloads, `PsramJsonDocument`.
5. Always set `Cache-Control: no-store` on responses (the helpers do this).
6. Document the endpoint in [API](API).

## Adding a new AI mode

Modes are data, not code. PUT a new mode into `ai.modes` via `/api/config`. The UI dropdown picks it up on next config load.

If you want a built-in default seeded for fresh installs, add it to `defaultAiModes()` in `Models.h`. Keep the list short — the seeded modes are what users see before they customize.

## Adding a field to `Item`

1. Add the field to `struct Item` in `Models.h`.
2. Update serialization in `InventoryStore::_writeItemJson()` and any export-related code.
3. Update parsing in `WebServerLL.cpp` (search for `out.tags.clear()` for the deserializer).
4. Bump the import-bundle `version` only if the change is breaking. Otherwise keep it backward-compatible by making the field optional.
5. Update the schema in [API](API) and [README](https://github.com/...).

## Testing

There is no unit test suite. Test on real hardware. Minimum smoke test before merging:

1. Fresh flash with **Erase All Flash**.
2. Confirm boot serial: PSRAM detected, AP up, web auth working.
3. Add ~5 items via UI. Reboot. Confirm they persist.
4. STA join. Confirm mDNS resolves.
5. Export JSON, delete all, import the export, confirm round-trip equality.
6. If touching AI:
   - `POST /api/ai/test` → `done=true, ok=true`.
   - `POST /api/ai/ask` with `mode=can_make_now` and a real inventory loaded.
   - Confirm `inventoryCacheHit=true` on the second request.
7. Watch serial for `[HEAP] WARNING` or `CRITICAL` during AI runs.

## Debugging tips

### Enable verbose AI logging

Already on. Look for `[AI]` lines:

```
[AI] GET http://192.168.5.250:1234/v1/models
[AI] Mode: can_make_now, restricted=1
[AI] Thinking disabled via /no_think directive
[AI] Payload: 18432 chars, inventory: 14820 chars (217/217 items), max_tokens=8192
```

### Inspect raw LM Studio traffic

From a desktop on the same LAN:

```bash
curl -v http://<lmstudio-ip>:1234/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{"model":"qwen2.5-7b-instruct","messages":[{"role":"user","content":"hi"}],"max_tokens":50}'
```

If this works but the ESP32 path doesn't, the ESP32 can't reach that host (firewall, subnet, AP isolation).

### Check heap and PSRAM live

`GET /api/storage` returns current `heapFree`, `heapUsed`, plus filesystem stats. Curl it before/during/after a heavy AI run.

## Pull requests

- Keep changes small and focused. One feature or one fix per PR.
- Test on real hardware (S3 + LittleFS + PSRAM) before submitting.
- Match existing style (function-based, threading-friendly, PSRAM-aware).
- Don't add cloud / SaaS dependencies. Local-first is a feature.
- If a change affects the API or config schema, update the [README](https://github.com/...) and the [API wiki page](API).

## License

GPL-3.0 — see source headers.
