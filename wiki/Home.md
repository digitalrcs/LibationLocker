# Libation Locker by Dan Roberts

Libation Locker is a **local, self-hosted ESP32 inventory web app** for tracking spirits / bottles.  
It runs fully on-device: a single-page UI, a small REST API, and persistent storage in **LittleFS**.

## Highlights
- AP-first (works without your home Wi‑Fi)
- Optional STA join (AP+STA), DHCP hostname: **LibationLocker**
- mDNS: **`http://libationlocker.local/`** (when on your LAN)
- Inventory CRUD + import/export
- Shopping list extract (Need=YES)

## Quick links
- [[Installation]]
- [[API]]
- [[Architecture]]
- [[Troubleshooting]]
- [[Development]]

## Security model
This project is designed for **trusted local networks**. There is no authentication by default.  
If you expose it beyond your LAN, add auth and TLS termination (reverse proxy).
