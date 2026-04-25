# LibationLocker — AI Streaming + Cancel Update

## What changed

The AI request path was rebuilt to use **Server-Sent Events (SSE) streaming**
instead of a single blocking `http.POST`. This fixes the timeout problem
where the ESP32's TCP socket sat idle while OpenAI computed the response,
tripping the read timeout regardless of how high `aiTimeoutSec` was set.

Tokens now arrive every ~10–50 ms, so the socket is never idle and the
read timeout never fires on long generations.

A cooperative **cancel** path was also added — the browser can stop a
running AI job via `POST /api/ai/cancel`, and the ESP32 will close the
upstream connection cleanly between chunks.

## Files modified

| File | What changed |
|------|--------------|
| `Models.h` | New `bool aiStreamingEnabled = true;` in `DropdownConfig` |
| `InventoryStore.cpp` | Persist `aiStreamingEnabled` on save / load / import (4 sites) |
| `AiAssistant.h` | New `AiStreamCallbacks` struct, new `askWithCallbacks()` public method, three new `AiAskResponse` fields (`streamed`, `cancelled`, `bytesReceived`), `jsonEscapeInto` made public |
| `AiAssistant.cpp` | New `askStreamed()` (SSE parser), shared `buildChatPayload()` and `extractSseDelta()` helpers, new dispatchers (`ask` and `askWithCallbacks`), cooperative cancel in `readResponseBody`, refactored old `ask()` body into `askBuffered()` |
| `WebServerLL.cpp` | New `/api/ai/cancel` endpoint, `AiJobState` extended with cancel/progress fields, `aiJobTask` wires streaming callbacks, `aiJobStatusJson()` exposes progressive answer + bytes |

## New REST API

### `POST /api/ai/cancel`
Set the cancel flag on the running AI job. The task aborts on the next
chunk boundary (typically within 10–100 ms).

Response — running job:
```json
{ "ok": true, "cancelRequested": true, "token": "..." }
```
Response — no active job:
```json
{ "ok": false, "error": "no_active_job" }
```

### `GET /api/ai/status` — new fields
The existing endpoint now returns these additional fields:

| Field | Type | Meaning |
|-------|------|---------|
| `streamed` | bool | True if the SSE path was used |
| `cancelRequested` | bool | True after cancel was requested but before task observed |
| `cancelled` | bool | True on final state if the task was aborted by user |
| `bytesReceived` | uint32 | Raw bytes from upstream socket so far |
| `lastChunkAt` | uint32 | `millis()` of the last received chunk |
| `partialAnswer` | string | Present **only while busy**: answer text accumulated so far |

When `done === true`, the final answer is in `answer` (as before),
and `partialAnswer` is cleared.

## Front-end integration sketch

Replace the existing "poll until done" loop with one that also reads
`partialAnswer` and offers a cancel button:

```javascript
async function askAi(question, mode, onChunk, onDone) {
  // Start the job
  const start = await fetch('/api/ai/ask', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ question, mode, includeInventory: true })
  }).then(r => r.json());

  if (!start.ok) throw new Error(start.error);
  const myToken = start.token;
  let lastSeen = '';

  // Poll
  while (true) {
    await new Promise(r => setTimeout(r, 250));
    const s = await fetch('/api/ai/status').then(r => r.json());
    if (s.token !== myToken) break;            // someone else started a job

    // Progressive update
    const current = s.partialAnswer || s.answer || '';
    if (current.length > lastSeen.length) {
      onChunk(current.slice(lastSeen.length));
      lastSeen = current;
    }

    if (s.done) { onDone(s); break; }
  }
}

async function cancelAi() {
  const r = await fetch('/api/ai/cancel', { method: 'POST' });
  return r.json();
}
```

## Configuration

`aiStreamingEnabled` defaults to `true`. To revert to the old buffered
path (e.g. for an OpenAI-compatible server that doesn't support SSE),
set it to `false` in your config.

## Provider compatibility

| Provider | Streaming | Notes |
|----------|-----------|-------|
| OpenAI | ✅ | Uses `stream:true` + `stream_options:{include_usage:true}` |
| LM Studio | ✅ | Same wire format as OpenAI |
| openai_compat | ✅ | Most servers (Groq, Together, OpenRouter, Mistral) support it |
| Anthropic | ✅ | Different SSE event shape (`content_block_delta`), parser handles it |

## Why not OpenAI's `/v1/batches`?

OpenAI does have a true batch endpoint (`POST /v1/batches`) with a
`batch_id` + polling pattern, but:
- 24-hour SLA (5–30 min minimum typical)
- Requires uploading a JSONL via `/v1/files` first (multipart form-data)
- Designed for offline cost-discounted jobs, not interactive use

For an interactive bartender chatbot, SSE streaming is the right tool.

## Memory / heap impact

- New SSE line buffer: 8 KB allocated per request (heap, freed on return)
- New `partialAnswer` String in `AiJobState`: grows during streaming, freed when job completes
- Per-chunk `StaticJsonDocument<4096>` on the stack inside `extractSseDelta`
- No new persistent allocations
