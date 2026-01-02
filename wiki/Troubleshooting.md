# Troubleshooting

## LittleFS mount failures
Serial shows:
- `esp_littlefs: partition "spiffs" could not be found`
- `Failed to initialize LittleFS`

Fix checklist:
1. Confirm partition table includes filesystem labeled `spiffs` with subtype `littlefs`.
2. Ensure **Flash Size = 16MB** in Arduino IDE.
3. One-time: **Erase Flash → All Flash Contents** after changing partitions.
4. Call `GET /api/fs` and verify `/data/` is present.

## Import seems to work but doesn’t persist
If items appear after refresh but disappear after reboot:
- Persistence is failing.
- Call `GET /api/fs` and confirm `/data/inventory.json` exists and grows.
- Review Serial logs for LittleFS errors.

## mDNS hostname not resolving
`http://libationlocker.local/` requires mDNS:
- Windows often needs Bonjour (iTunes installs it).
- Some routers/VLANs block multicast; try same LAN segment or use the printed IP.

## Upload failures / boot mode
- Hold **BOOT**, tap **RESET**, then upload.
- Use a known-good **data** USB cable and avoid flaky hubs.
