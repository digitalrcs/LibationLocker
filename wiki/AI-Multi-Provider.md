# AI: Multi-Provider Setup

LibationLocker's AI assistant supports four providers behind a unified config. Pick whichever matches your priorities for cost, privacy, latency, and quality.

## At a glance

| Provider | `provider` value | Endpoint | API key | Cost per 1M tokens (in/out)* | Privacy | Speed | Quality |
|---|---|---|---|---|---|---|---|
| **LM Studio** (default) | `lmstudio` | local HTTP | none | $0 | local-only | depends on hardware | depends on model |
| **OpenAI** | `openai` | `https://api.openai.com` | required | $0.15–$15 / $0.60–$60 | data sent to OpenAI | seconds | very high (GPT-4o, o3) |
| **Anthropic** | `anthropic` | `https://api.anthropic.com` | required | $0.80–$15 / $4–$75 | data sent to Anthropic | seconds | very high (Claude Sonnet/Opus) |
| **OpenAI-compatible** | `openai_compat` | user-supplied | depends | varies | depends on host | varies | varies |

\* Indicative ranges; check the provider's pricing page for current numbers.

## Switching providers

Same config endpoint, just change `ai.provider` and the related fields:

```bash
curl -u admin:admin -X PUT http://libationlocker.local/api/config \
  -H "Content-Type: application/json" \
  -d '{
    "types":["Bourbon","Rye","Gin","Tequila"],
    "sizesMl":[375,750,1000,1750],
    "abvPresets":[40,45,50],
    "remainingPresets":[100,75,50,25,0],
    "ai":{
      "enabled": true,
      "provider": "anthropic",
      "baseUrl": "https://api.anthropic.com",
      "model": "claude-sonnet-4-5",
      "apiKey": "sk-ant-...",
      "temperature": 0.2,
      "maxTokens": 4096,
      "timeoutSec": 60
    }
  }'
```

Then verify:

```bash
curl -u admin:admin http://libationlocker.local/api/ai/models
curl -u admin:admin -X POST http://libationlocker.local/api/ai/test
curl -u admin:admin http://libationlocker.local/api/ai/status
```

The same five built-in modes (`general`, `can_make_now`, `missing_ingredients`, `recommend_purchases`, `shopping_list`) work across all providers.

---

## LM Studio (default)

**Use when:** you want everything local, free, and private.

See the dedicated [AI Assistant](AI-Assistant) page for the full LM Studio setup. Key fields:

```json
{
  "provider": "lmstudio",
  "baseUrl": "http://192.168.5.250:1234",
  "model": "qwen2.5-7b-instruct",
  "apiKey": "",
  "disableThinking": true
}
```

The `disableThinking` flag appends ` /no_think` to the system prompt, which Qwen / DeepSeek-R1 honor. **It's only sent to LM Studio** — for cloud providers it's ignored automatically.

---

## OpenAI

**Use when:** you want top-tier quality, fast responses, and you're OK paying per-request.

### One-time setup

1. Get an API key at https://platform.openai.com/api-keys.
2. Top up the account with credits (pay-as-you-go).
3. Set the AI config:

```json
{
  "enabled": true,
  "provider": "openai",
  "baseUrl": "https://api.openai.com",
  "model": "gpt-4o-mini",
  "apiKey": "sk-...",
  "temperature": 0.2,
  "maxTokens": 4096,
  "timeoutSec": 60,
  "tlsInsecure": true
}
```

### Model recommendations

| Model | When | Cost (in/out per 1M tokens, approximate) |
|---|---|---|
| `gpt-4o-mini` | Daily driver. Fast, cheap, plenty for cocktail Q&A. | ~$0.15 / $0.60 |
| `gpt-4o` | Higher quality, still fast. | ~$2.50 / $10 |
| `gpt-5-mini` | If available on your account, smaller/cheaper than full gpt-5. | varies |
| `o3-mini` | Reasoning model. Slow and expensive — overkill for cocktail recommendations. | ~$1.10 / $4.40 |

The firmware automatically:
- Filters out non-chat models (embeddings, TTS, whisper, dall-e, moderation, realtime) from the `/api/ai/models` dropdown.
- Sends `max_completion_tokens` instead of `max_tokens` for o1/o3/o4 / gpt-5 models (they reject `max_tokens`).
- Drops `temperature` from the payload for o1/o3/o4 (they only accept the default value).

### Notes

- HTTPS is mandatory. The firmware uses `WiFiClientSecure` with `setInsecure()` by default; the API key is the real auth.
- Inventory is sent to OpenAI on every request (subject to OpenAI's data policy). If that's a problem, use LM Studio instead.
- Typical cocktail Q&A with full inventory: 10–30 KB inbound prompt, 1–4 KB response. At gpt-4o-mini rates, **fractions of a cent per question**.

---

## Anthropic Claude

**Use when:** you want top-tier reasoning quality, prefer Anthropic's response style, or want a second opinion alongside OpenAI.

### One-time setup

1. Get an API key at https://console.anthropic.com/.
2. Top up with credits.
3. Set the AI config:

```json
{
  "enabled": true,
  "provider": "anthropic",
  "baseUrl": "https://api.anthropic.com",
  "model": "claude-sonnet-4-5",
  "apiKey": "sk-ant-...",
  "temperature": 0.2,
  "maxTokens": 4096,
  "timeoutSec": 60,
  "tlsInsecure": true
}
```

### Model recommendations

| Model | When | Notes |
|---|---|---|
| `claude-haiku-4-5` | Fast and cheap daily driver. | Good for straightforward inventory questions. |
| `claude-sonnet-4-5` | Best balance. Smart, fast, reasonable price. | Recommended default. |
| `claude-opus-4-7` | Highest quality, most expensive. | Overkill unless you want long-form recipe reasoning. |

Use `GET /api/ai/models` to confirm what's available on your account — Anthropic adds models periodically and the canonical id may change.

### Why Anthropic is wired separately

Anthropic's API differs from OpenAI's in several ways the firmware handles transparently:

| What | OpenAI / LM Studio | Anthropic |
|---|---|---|
| Endpoint | `POST /v1/chat/completions` | `POST /v1/messages` |
| Auth header | `Authorization: Bearer <key>` | `x-api-key: <key>` |
| Required version header | (none) | `anthropic-version: 2023-06-01` |
| System prompt | inside `messages[]` array | top-level `system` field |
| Token field | `max_tokens` (or `max_completion_tokens`) | `max_tokens` (required) |
| Response shape | `choices[0].message.content` | `content[0].text` (array of blocks) |
| Stop reason | `finish_reason` | `stop_reason` |

The provider dispatch in `AiAssistant::ask()` handles all of these automatically. Modes, inventory serialization, and the async job pattern are unchanged.

### Notes

- HTTPS only. Same `WiFiClientSecure` + `setInsecure()` defaults as OpenAI.
- Anthropic doesn't expose a `reasoning_content` field; the `disableThinking` flag is silently ignored.
- Inventory + prompt is sent to Anthropic on every request. Same privacy considerations as OpenAI.

---

## OpenAI-compatible (Groq, Together, OpenRouter, Mistral, Gemini, vLLM, etc.)

**Use when:** you want a non-OpenAI cloud provider that exposes an OpenAI-compatible endpoint, or you're self-hosting an OpenAI-compat shim.

```json
{
  "enabled": true,
  "provider": "openai_compat",
  "baseUrl": "https://api.groq.com/openai",
  "model": "llama-3.3-70b-versatile",
  "apiKey": "gsk_...",
  "temperature": 0.2,
  "maxTokens": 4096,
  "timeoutSec": 60,
  "tlsInsecure": true
}
```

### Confirmed-working examples

| Provider | `baseUrl` | Auth | Notes |
|---|---|---|---|
| **Groq** | `https://api.groq.com/openai` | `Authorization: Bearer <key>` | Very fast (LPU inference). Free tier exists. |
| **Together AI** | `https://api.together.xyz` | `Authorization: Bearer <key>` | Wide model catalog (Llama, Mistral, Qwen, etc.). |
| **OpenRouter** | `https://openrouter.ai/api` | `Authorization: Bearer <key>` | Single key, many models including Claude/GPT via routing. |
| **Mistral** | `https://api.mistral.ai` | `Authorization: Bearer <key>` | Native OpenAI-compat endpoint. |
| **Google Gemini (OpenAI-compat)** | `https://generativelanguage.googleapis.com/v1beta/openai` | `Authorization: Bearer <key>` | Google's OpenAI-shim layer. |
| **xAI Grok** | `https://api.x.ai` | `Authorization: Bearer <key>` | OpenAI-compat. |
| **Self-hosted vLLM / Ollama OpenAI-compat** | depends | depends | Use `http://` if on same LAN, no TLS needed. |

### Notes

- The firmware sends OpenAI Chat Completions shape and parses OpenAI Chat Completions response. If your provider deviates, the response parser falls back to OpenAI's Responses API shape (`output_text`, `output[].content[].text`) — this covers most edge cases.
- Use HTTP (no TLS) only if the provider is on your LAN. For anything on the public internet, use `https://` so transport is encrypted.
- Provider-specific quirks (tools, function calling, vision) are not used by LibationLocker — only plain text completion is needed.

---

## Choosing a provider

Some questions to ask:

1. **Is the data sensitive?** Liquor inventory is harmless, but if you've added unusual notes or if the device is in a shared/work environment, prefer LM Studio.
2. **Do I want zero recurring cost?** LM Studio. (Hardware capex once, then free.)
3. **Do I want the best possible answer quality?** Anthropic Sonnet/Opus or OpenAI gpt-4o.
4. **Do I want fast (<5s) responses?** Any cloud provider. Groq is fastest. LM Studio on CPU is 30–300s for 7B models.
5. **Am I behind a firewall / on a flaky LAN?** LM Studio + LAN. Cloud providers need outbound HTTPS.

You can switch providers at any time without losing inventory or modes — they're independent of provider config.

## Cost back-of-envelope

A typical "what can I make tonight" question with 200-item inventory:

- Inbound prompt: ~15 KB ≈ 4,000 tokens
- Outbound answer: ~2 KB ≈ 500 tokens

| Provider/model | Cost per question |
|---|---|
| LM Studio (Qwen2.5-7B local) | $0 |
| OpenAI gpt-4o-mini | ~$0.0009 |
| OpenAI gpt-4o | ~$0.015 |
| Anthropic Claude Haiku | ~$0.005 |
| Anthropic Claude Sonnet | ~$0.020 |
| Groq llama-3.3-70b (paid) | ~$0.003 |

So 100 questions/month at gpt-4o-mini ≈ **9 cents**. At Sonnet ≈ **$2**. Cheap enough that the choice should be quality and privacy, not cost.

## Troubleshooting

### `<Provider> requires an API key (set ai.apiKey in config)`

Cloud providers require a key. LM Studio and `openai_compat` against your own LAN host don't.

### `Failed to connect to <Provider> at <host>:443 (TLS)`

Several possible causes:
- ESP32 isn't on a network with internet access (`WiFi.status()` is connected to AP-only)
- DNS resolution failing — try the IP directly (rare, but `api.openai.com` is multi-A-record)
- TLS handshake exhausting heap. Confirm PSRAM is enabled. Check `[BOOT] PSRAM total=...` in serial.
- Captive portal / corporate proxy intercepting HTTPS — ESP32 with `setInsecure()` doesn't validate, but the proxy may inject a redirect that breaks the request.

### `<Provider> returned HTTP 401`

API key is wrong, expired, or revoked. Generate a new one in the provider's console.

### `<Provider> returned HTTP 400: ...max_tokens...`

OpenAI o1/o3/o4 reject `max_tokens` and require `max_completion_tokens`. The firmware detects model id prefixes and switches automatically — if you see this, the model id starts with something we didn't recognize. Open an issue with the model name.

### `Configured model not found in <Provider>: <model-id>`

The model id doesn't exist in this provider's `/v1/models` list. Run `GET /api/ai/models` to see what's actually available, then PUT the right id into config. Note: OpenAI hides non-chat models from the dropdown (embeddings/TTS/etc.), so if you specifically configured one of those, change to a chat model.

### Heap warnings during HTTPS requests

```
[HEAP] WARNING free=24816 maxAlloc=18432
```

TLS handshake on ESP32 transiently uses ~25–30 KB of internal heap. With PSRAM enabled and a typical inventory, this should be fine. If you see CRITICAL warnings:
- Confirm PSRAM is actually enabled (check `[BOOT] PSRAM total=8388608`).
- Reduce inventory size or split into multiple devices.
- Consider switching to a non-TLS provider (LM Studio on LAN).
