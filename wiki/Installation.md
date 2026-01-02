# Installation

## Hardware
- ESP32‑S3 **N16R8** (16MB flash, 8MB PSRAM)

## Arduino IDE settings (recommended)
- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**
- PSRAM: **Enabled** (OPI if available)
- Erase Flash: **All Flash Contents** (one-time after changing partitions/filesystem)

## Partition table (LittleFS)
This repo includes a `partitions.csv` with:
- Factory app partition
- Filesystem partition labeled **`spiffs`** with subtype **littlefs**

If you create your own partitions, keep the filesystem label as `spiffs` unless you also change the mount label in code.

## First boot
1. Flash firmware.
2. Device brings up AP (SSID shown in Serial).
3. Browse to `http://192.168.4.1/` and configure STA Wi‑Fi (optional).
4. After STA join, access:
   - `http://libationlocker.local/` (mDNS)
   - or the device IP printed to Serial.

## Verify persistence
Call `GET /api/fs` and confirm you see `/data/inventory.json` after adding an item.
