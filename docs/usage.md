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
