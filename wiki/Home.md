# LibationLocker Wiki

Welcome. LibationLocker is a self-hosted ESP32-S3 inventory web app for spirits/bottles, with an optional on-device AI assistant that talks to a local LM Studio instance.

## Pages

- [Installation](Installation) — flashing, partitions, first boot
- [API](API) — every REST endpoint, with `curl` examples
- [Architecture](Architecture) — how it's wired internally, FreeRTOS layout, memory model
- [AI Assistant](AI-Assistant) — LM Studio setup, mode design, request lifecycle, tuning guide
- [AI Multi-Provider](AI-Multi-Provider) — using OpenAI ChatGPT, Anthropic Claude, Groq, OpenRouter, Mistral, etc.
- [Troubleshooting](Troubleshooting) — common failure modes and fixes
- [Development](Development) — contributing, code style, testing on real hardware

## Quick links

- AP IP: `http://192.168.4.1`
- mDNS: `http://libationlocker.local/`
- Default login: `admin` / `admin` (**change before sharing the device**)
- Default AP password: `adminadmin` (**change before sharing the device**)

## What this is not

- Not a cloud service. Nothing leaves your LAN.
- Not a multi-user app. Single shared admin login is by design for a home device.
- Not encrypted in transit. HTTP-only. Use on trusted networks.

## Project status

Active personal project by Dan Roberts. PRs welcome — see [Development](Development).
