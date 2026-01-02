# Development

## Repo layout
- `LibationLocker.ino`: app bootstrap
- `WebServerLL.*`: routes + API + UI delivery
- `WebUiAssets.h`: embedded HTML/CSS/JS
- `InventoryStore.*`: model + persistence + import/export
- `AppWiFi.*`: AP/STA + hostname + mDNS
- `AppConfig.*`: saved settings, dropdown presets

## Coding conventions
- Prefer small, testable helpers.
- Keep API handlers thin; push logic into InventoryStore.
- Use temp-file + rename for filesystem writes.

## Suggested enhancements
- Optional auth token (simple shared secret)
- CSV import
- Pagination for very large inventories
- UI grouping/sorting/pinning
