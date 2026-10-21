# Usage

## Command Structure

```bash
gs <command> [options]
gs analyze <owner/repo> [options]
gs watch <subcommand> [options]
gs sub <owner/repo> [options]
gs config [options]
gs completion <bash|zsh|fish>
gs version
```

## Analyze Command

### Basic

```bash
# Analyze last 7 days, markdown output to stdout
gs analyze rust-lang/rust

# Analyze last 30 days
gs analyze rust-lang/rust --since 30

# JSON output
gs analyze owner/repo --format json

# Plain text output
gs analyze owner/repo --format txt
```

### Repository Formats

`owner/repo` accepts various formats:

```bash
# Simple
gs analyze curl/curl

# Full URL
gs analyze https://github.com/curl/curl

# With .git suffix
gs analyze https://github.com/curl/curl.git

# SSH format
gs analyze git@github.com:curl/curl.git
```

### Output Options

```bash
# Save to file
gs analyze owner/repo -f md -o report.md

# Multiple formats at once
gs analyze owner/repo -f json,md,txt -o ./report

# XLSX / PDF (requires -o, cannot write to stdout)
gs analyze owner/repo -f xlsx -o report.xlsx
gs analyze owner/repo -f pdf -o report.pdf

# Mix binary and text formats
gs analyze owner/repo -f xlsx,md -o ./report
gs analyze owner/repo -f pdf,json -o ./report

# Pipe to other tools (e.g., glow for markdown rendering)
gs analyze owner/repo | glow -
gs analyze owner/repo --format json | jq .
```

When piping, progress messages go to stderr so they won't interfere with the output.

### Network Options

```bash
# HTTP proxy (for restricted networks)
gs analyze owner/repo --proxy http://127.0.0.1:7890

# Custom API base (GitHub Enterprise or mirror)
gs analyze owner/repo --api-base https://api.github.com

# Or via environment variables
export HTTP_PROXY=http://127.0.0.1:7890
export GS_API_BASE=https://api.github.com
```

### Authentication

```bash
# Via CLI argument
gs analyze owner/repo --token ghp_xxxxxxxxxxxx

# Via environment variable
export GITHUB_TOKEN=ghp_xxxxxxxxxxxx

# Persist in config file
gs config --set-token ghp_xxxxxxxxxxxx
```

Priority: `--token` argument > config file > `GITHUB_TOKEN` env var.

### Color Control

```bash
# Force colors
gs analyze owner/repo --color always

# Disable colors
gs analyze owner/repo --color never

# Auto (default): enabled when stdout is a terminal
gs analyze owner/repo --color auto

# Via environment
export NO_COLOR=1
```

### LLM-Powered Analysis

```bash
# Use the active LLM provider from config
gs analyze owner/repo

# Use a specific provider for this run only
gs analyze owner/repo --llm openai

# Via environment variables (creates/overrides active provider)
export LLM_API_KEY=sk-xxx
export LLM_API_BASE=https://api.openai.com/v1
export LLM_MODEL=gpt-4o-mini
gs analyze owner/repo
```

See [Configuration](configuration.md) for detailed multi-provider setup and custom prompts.

## Config Command

```bash
# Show config file path
gs config

# Save GitHub token permanently
gs config --set-token ghp_xxxxxxxxxxxx

# List available LLM providers
gs config --list-llms

# Set active LLM provider (persistent)
gs config --set-llm local-ollama
```

### Export Configuration

```bash
# Hide a section
gs analyze owner/repo --export show_commits=false

# Customize PDF layout
gs analyze owner/repo -f pdf -o report.pdf --export pdf.margin=40

# Customize PDF colors (requires --color always)
gs analyze owner/repo -f pdf -o report.pdf --color always --export pdf.title_color=200,50,50

# Customize XLSX
gs analyze owner/repo -f xlsx -o report.xlsx --export xlsx.font_size=14 --export xlsx.border=false

# Combine with other options
gs analyze owner/repo -f pdf,json -o ./report --export show_issues=false --export pdf.margin=30
```

See [Configuration](configuration.md) for all available export options.

### Custom Prompt Directory

Place custom system prompt files in `~/.config/gitsight/prompts/`:

```
~/.config/gitsight/prompts/
  security_review.md
  changelog_gen.md
```

Reference them in each LLM provider's `system_prompt_file` field.
See [Configuration](configuration.md) for details.

## Version Command

```bash
gs version
# GitSight version 0.1.0
```

## Output Examples

### Markdown

```
# GitSight Report

**Repository**: curl/curl
**Period**:     last 7 days

## [PR] Pull Requests

| Metric | Value |
|--------|-------|
| Total  | 42 |
| Merged | 35 |
| Open   | 5 |
| Closed | 2 |

## [CM] Commits

| Metric      | Value |
|-------------|-------|
| Total       | 156 |
| Authors     | 48 |

## [IS] Issues

| Metric | Value |
|--------|-------|
| Total  | 28 |
| Open   | 15 |
| Closed | 13 |

## [*] Summary

The repository has been active over the last 7 days...
```

### JSON

```json
{
    "owner": "curl",
    "repo": "curl",
    "period": "last 7 days",
    "pull_requests": {
        "total": 42, "merged": 35,
        "open": 5, "closed": 2
    },
    "commits": {
        "total": 156, "authors": 48
    },
    "issues": {
        "total": 28, "open": 15, "closed": 13
    },
    "summary": "..."
}
```

### Plain Text

```
+------------------------------------------------------+
|                    GitSight Report                    |
+------------------------------------------------------+
| Repository:  curl/curl                                |
| Period:      last 7 days                              |
+------------------------------------------------------+
| [PR] Pull Requests                                    |
|   Total:   42                                         |
|   Merged:  35                                         |
...
+------------------------------------------------------+
```

## LLM Usage

Every successful LLM call records its token usage (prompt/completion/total) to a
local log next to the config file (`usage.json`, or `$GS_USAGE_FILE`). View it
as a unicode bar chart:

```bash
# Daily token usage for the last 14 days (vertical bar chart)
gs usage

# Adjust the window
gs usage --days 30

# Group by LLM provider (horizontal bars)
gs usage --by-llm

# Raw aggregated data
gs usage --json

# Reset the log
gs usage --clear
```

Example output:

```
LLM usage - last 14 days

      █ █ █
    █ █ █ █ █
  █ █ █ █ █ █ █
  08-20 08-21 08-22 08-23
  150 300 450 900

Total: 12 calls, 1800 tokens (1200 prompt + 600 completion)
```

Set `GS_NO_USAGE=1` to stop recording usage entirely.

## Web Console

`gs serve` starts a local HTTP daemon that serves a browser console covering
every command above — analysis, comparison, batch runs, subscriptions, LLM
providers, settings, usage and shell completion.

```bash
gs serve                      # http://127.0.0.1:8787
gs serve --port 9000
gs serve --config ./scratch/config.json   # isolated settings
```

| Option | Description |
|---|---|
| `--host <addr>` | Address to bind. Defaults to `127.0.0.1`. |
| `--port <n>` | Port to listen on. Defaults to `8787`. Also `GS_PORT`. |
| `--web-root <dir>` | Directory holding the built console. Also `GITSIGHT_WEB_ROOT`. |
| `--config <path>` | Config file to use. Defaults to `~/.config/gitsight/config.json`. |
| `--jobs <n>` | Concurrent analysis jobs. |
| `--quiet`, `-q` | Suppress the startup banner. |

The daemon binds to loopback and has **no authentication** — it reads and writes
the same config file the CLI uses, including access tokens. Only pass
`--host 0.0.0.0` on a network you trust.

### Building the console

```bash
npm --prefix web install
npm --prefix web run build          # → web/dist
cp -r web/dist build/web            # gs serve finds ./web by default

# or let CMake do both (needs Node):
cmake -B build -DGITSIGHT_BUILD_WEB=ON
cmake --build build
```

### Development

```bash
gs serve --port 8787                # API on one port
npm --prefix web run dev            # Vite on :5173, proxying /api → :8787
```

`GS_API_TARGET` overrides the proxy target if the daemon runs elsewhere.

### What the console adds over the CLI

- **Live progress.** Analyses run as jobs whose stage log streams to the
  browser over SSE; a run can be cancelled mid-flight.
- **Downloads without refetching.** A finished job keeps its data in memory, so
  switching export format renders instantly from the same result.
- **Cross-repository parallelism.** A batch analyses up to 8 repositories at
  once, each fetching its seven resources serially.
- **Interactive reports.** The report is rendered natively with filterable
  tables and charts, following the same layout and state colours as the HTML
  export.

### Environment

`GS_FIXTURE_DIR` works with the daemon too, which is how `tests/test_server.sh`
exercises the entire API offline.

## HTTP API

The daemon exposes the API the console uses. It is stable enough to script
against.

```
GET    /api/health                    version, paths, uptime, active jobs
GET    /api/meta/options              formats, platforms, colours, limits
GET    /api/meta/i18n                 the report body's EN/ZH table
GET    /api/quota · /api/cache        rate-limit snapshot; cache location

POST   /api/jobs/analyze              → 202 {job_id}
POST   /api/jobs/compare              → 202 {job_id}
GET    /api/jobs/:id                  state + progress log
GET    /api/jobs/:id/result           the report JSON
GET    /api/jobs/:id/events           SSE: progress / stage-done / done
GET    /api/jobs/:id/export?format=   json|md|txt|html|csv|xlsx|pdf
POST   /api/jobs/:id/cancel

GET    /api/subscriptions             POST add · PATCH/DELETE /:ref
POST   /api/subscriptions/analyze     batch, with per-entry overrides
POST   /api/subscriptions/:ref/analyze

GET    /api/config                    tokens redacted to {set, masked}
PATCH  /api/config                    {"sets": {...}}, validated server-side
GET/PUT /api/config/raw               POST /api/config/reset

GET    /api/llms                      POST create · PATCH/DELETE /:name
POST   /api/llms/:name/activate · /test

GET    /api/usage?days= · /records    DELETE /api/usage
                                  /records takes days=, so the log matches the charts
GET    /api/completion/:shell         bash | zsh | fish

GET    /*                             the built console (SPA fallback)
```

### Job request body

`POST /api/jobs/analyze` and `POST /api/jobs/compare` take the same per-run
options, and each one overrides the config file for that job only:

| Field | Meaning |
|---|---|
| `since_days`, `from`, `to` | the window |
| `platform`, `branch`, `lang` | what to read, and the language to write in |
| `max_pages`, `api_base`, `proxy`, `token` | transport |
| `llm`, `llm_mode`, `no_cache` | the analysis itself |
| `filters` | `["author:alice", "label:bug"]` |
| `export` | `["show_commits=false", "pdf.margin=40"]` — exporter settings for this run |

The result carries the provider that wrote the summary as `llm_provider`; it is
empty when the local summary was used, which is the only honest answer when the
LLM failed. A comparison carries the same field for its trend summary.

`export` also accepts the object form `{"show_commits": false, "pdf": {"margin": 40}}`.
The names the exporter accepts are listed by `GET /api/meta/options` under
`export_keys`; an unknown name is ignored, so read the list before sending one.

Secrets never leave the process: config tokens and provider keys are returned as
`{"set": true, "masked": "…3f2a"}`, and writing an empty string clears one.
