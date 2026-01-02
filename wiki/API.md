# API

Base URL:
- AP mode: `http://192.168.4.1`
- STA mode: `http://libationlocker.local` (or device IP)

Content type:
- JSON endpoints use `Content-Type: application/json`

## Health
### `GET /api/health`
Returns a simple status payload.

```bash
curl http://192.168.4.1/api/health
```

## Network configuration
### `GET /api/net`
Returns AP/STA status and current STA config (if saved).

### `PUT /api/net`
Set STA credentials.

```bash
curl -X PUT http://192.168.4.1/api/net   -H "Content-Type: application/json"   -d '{"staSsid":"MyWifi","staPass":"Secret"}'
```

## Storage / filesystem
### `GET /api/storage`
Returns total/used/free bytes plus heap info.

### `GET /api/fs`
Lists files under `/` and `/data` (debug).

```bash
curl http://192.168.4.1/api/fs
```

## Inventory
### `GET /api/items`
Returns all items.

### `POST /api/item`
Create an item.

### `PUT /api/item?id=<id>`
Update an item (optimistic concurrency via `version`).

### `DELETE /api/item?id=<id>`
Delete an item.

Example create:
```bash
curl -X POST http://192.168.4.1/api/item   -H "Content-Type: application/json"   -d '{"type":"Bourbon","brand":"Maker's Mark","name":"Kentucky Straight","sizeMl":750,"abv":45,"qty":1,"remainingPct":100,"needToBuy":false,"rating":4,"tags":["cocktails"],"notes":""}'
```

## Dropdown config
### `GET /api/config`
### `PUT /api/config`

## Export
### `GET /api/export?format=json|csv|txt[&filter=need]`
- `format=json` (default) returns a full bundle (config + items)
- `filter=need|shopping|needtobuy` limits to `needToBuy=true`

Shopping list TXT:
```bash
curl -L "http://192.168.4.1/api/export?format=txt&filter=need" -o shopping-list.txt
```

## Import
### `POST /api/import?mode=merge|append|replace[&dryrun=1]`
- `merge`: upserts by `id`
- `append`: adds as new items (assign new ids)
- `replace`: replaces inventory + config with payload
- `dryrun=1`: validates only; no writes

```bash
curl -X POST "http://192.168.4.1/api/import?mode=merge"   -H "Content-Type: application/json"   --data-binary "@libationlocker-export.json"
```
