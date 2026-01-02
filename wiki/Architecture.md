# Architecture

## High-level
- **WebServerLL**: routes, serves UI, implements REST API
- **InventoryStore**: in-memory model + persistence (LittleFS) + import/export
- **AppWiFi**: AP + optional STA join, DHCP hostname, mDNS service
- **AppConfig**: runtime config (STA credentials, UI dropdown presets)

## Persistence
- LittleFS mounted at boot (lazy-mount on first access)
- Data files:
  - `/data/inventory.json`
  - `/data/config.json`

Writes use a **temp-file + rename** strategy to reduce corruption risk on power loss.

## Concurrency / safety
- InventoryStore operations are protected by a mutex (`withLock`) to avoid races between:
  - HTTP requests
  - scheduled UI refresh/polls
