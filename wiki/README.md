# Wiki source

These markdown files mirror what the GitHub Wiki should contain. GitHub Wiki is a separate Git repo; you can either:

## Option A — Paste into the GitHub Web UI

1. On your repo: **Wiki** tab → **Create the first page**.
2. For each `.md` file in this folder, create a new page with the same name (without `.md`) and paste the contents.
3. Page name to filename mapping:
   - `Home` → `Home.md`
   - `Installation` → `Installation.md`
   - `API` → `API.md`
   - `Architecture` → `Architecture.md`
   - `AI Assistant` → `AI-Assistant.md` (GitHub renders the dash as a space)
   - `Troubleshooting` → `Troubleshooting.md`
   - `Development` → `Development.md`

## Option B — Git push (faster for updates)

GitHub Wiki has its own clone URL: `https://github.com/<user>/<repo>.wiki.git`.

```bash
git clone https://github.com/<user>/LibationLocker.wiki.git
cd LibationLocker.wiki
cp ../LibationLocker/wiki/*.md .
git add .
git commit -m "Sync wiki with code (AI assistant docs)"
git push
```

## Notes

- GitHub Wiki uses the **filename** (without `.md`) as the page title and URL slug.
- Spaces in titles are dashes in the URL (`AI Assistant` → `AI-Assistant`).
- Internal links between pages use the dashed form: `[AI Assistant](AI-Assistant)`.
- Don't rename `Home.md` — GitHub Wiki uses it as the landing page.
