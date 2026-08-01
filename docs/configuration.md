# Configuration

## Config File Location

`~/.config/gitsight/config.json` (JSON format)

Created automatically with defaults on first `gs config` run.

## Configuration Priority

1. CLI arguments (highest)
2. Config file
3. Environment variables
4. Built-in defaults (lowest)

## All Fields

```json
{
    "github_token": "ghp_xxxxxxxxxxxx",
    "api_base": "https://api.github.com",
    "proxy": "http://127.0.0.1:7890",
    "default_days": 7,
    "color": "auto",
    "format": "md",
    "active_llm": "openai",
    "llms": {
        "openai": {
            "api_key": "sk-xxxxxxxxxxxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "system_prompt": "You are a code analyst..."
        },
        "local": {
            "api_key": "not-needed",
            "api_base": "http://localhost:11434/v1",
            "model": "llama3.2",
            "system_prompt_file": "my_prompt.md"
        }
    },
    "export": {
        "show_prs": true,
        "show_commits": true,
        "show_issues": true,
        "show_summary": true,
        "show_top_labels": true,
        "md": { "table_width": 60 },
        "txt": { "table_width": 56 },
        "xlsx": {
            "font_size": 11,
            "font_name": "Calibri",
            "col_a_width": 20,
            "col_b_width": 60,
            "border": true,
            "sheet_name": "Report"
        },
        "pdf": {
            "margin": 50,
            "line_height": 1.4,
            "col2_padding": 12,
            "title_color": "25,55,160",
            "header_color": "25,55,160",
            "label_color": "90,90,90"
        }
    }
}
```

## Field Reference

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `github_token` | string | `""` | GitHub personal access token |
| `api_base` | string | `"https://api.github.com"` | GitHub API base URL (for mirrors/enterprise) |
| `proxy` | string | `""` | HTTP/HTTPS/SOCKS5 proxy URL |
| `default_days` | number | `7` | Default analysis period in days |
| `color` | string | `"auto"` | Color mode: `"auto"`, `"always"`, `"never"` |
| `format` | string | `"md"` | Default output format: `"md"`, `"json"`, `"txt"`, `"xlsx"`, `"pdf"` |
| `active_llm` | string | `""` | Active LLM provider name (must match a key in `llms`) |
| `llms.<name>.api_key` | string | `""` | LLM API key |
| `llms.<name>.api_base` | string | `"https://api.openai.com/v1"` | LLM API base URL |
| `llms.<name>.model` | string | `"gpt-4o-mini"` | LLM model name |
| `llms.<name>.system_prompt` | string | `""` | Custom system prompt (inline, overrides built-in) |
| `llms.<name>.system_prompt_file` | string | `""` | System prompt file in `prompts/` subdirectory (overrides `system_prompt`) |
| `export.show_prs` | bool | `true` | Show Pull Requests section |
| `export.show_commits` | bool | `true` | Show Commits section |
| `export.show_issues` | bool | `true` | Show Issues section |
| `export.show_summary` | bool | `true` | Show Summary section |
| `export.show_top_labels` | bool | `true` | Show Top Labels section |
| `export.md.table_width` | number | `60` | Markdown table width (used for text wrapping) |
| `export.txt.table_width` | number | `56` | Plain text table width in characters |
| `export.xlsx.font_size` | number | `11` | XLSX font size (pt) |
| `export.xlsx.font_name` | string | `"Calibri"` | XLSX font name |
| `export.xlsx.col_a_width` | number | `20` | XLSX column A width (characters) |
| `export.xlsx.col_b_width` | number | `60` | XLSX column B width (characters) |
| `export.xlsx.border` | bool | `true` | Enable XLSX cell borders |
| `export.xlsx.sheet_name` | string | `"Report"` | XLSX worksheet name |
| `export.pdf.margin` | number | `50` | PDF page margin (pt) |
| `export.pdf.line_height` | number | `1.4` | PDF line height multiplier (× font size) |
| `export.pdf.col2_padding` | number | `12` | PDF column 2 offset padding (pt) |
| `export.pdf.title_color` | string | `"25,55,160"` | PDF title text color (R,G,B) |
| `export.pdf.header_color` | string | `"25,55,160"` | PDF section header text color (R,G,B) |
| `export.pdf.label_color` | string | `"90,90,90"` | PDF metric label text color (R,G,B) |

## CLI Override

Export configuration can be overridden at runtime via `--export`:

```bash
# Hide a section
gs analyze owner/repo --export show_commits=false

# Customize PDF layout
gs analyze owner/repo --export pdf.margin=40 --export pdf.line_height=1.6

# Customize PDF colors
gs analyze owner/repo --color always --export pdf.title_color=200,50,50

# Customize XLSX
gs analyze owner/repo --export xlsx.font_size=14 --export xlsx.sheet_name="My Report"

# Combine multiple overrides
gs analyze owner/repo \
  --export show_issues=false \
  --export pdf.margin=30 \
  --export pdf.title_color=0,0,200
```

Note: PDF colors only take effect when outputting to a file with `--color always`.

## Environment Variables

| Variable | Overrides Config Field |
|----------|----------------------|
| `GITHUB_TOKEN` | `github_token` |
| `GS_API_BASE` | `api_base` |
| `HTTP_PROXY` | `proxy` |
| `HTTPS_PROXY` | `proxy` |
| `NO_COLOR` | `color` (disables color if set) |
| `LLM_API_KEY` | `active_llm.api_key` |
| `LLM_API_BASE` | `active_llm.api_base` |
| `LLM_MODEL` | `active_llm.model` |

## Multiple LLM Providers

The config file supports any number of named LLM providers. Switch between them with
`gs config --set-llm <name>` or `gs analyze <repo> --llm <name>`.

Example with two providers:

```json
{
    "active_llm": "openai",
    "llms": {
        "openai": {
            "api_key": "sk-xxxxxxxxxxxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini"
        },
        "local-llama": {
            "api_key": "not-needed",
            "api_base": "http://localhost:8080/v1",
            "model": "local-model"
        },
        "local-ollama": {
            "api_key": "ollama",
            "api_base": "http://localhost:11434/v1",
            "model": "llama3.2"
        }
    }
}
```

## Custom System Prompts

You can customize how the LLM analyzes repositories via system prompts.

### Inline Prompt

```json
{
    "llms": {
        "openai": {
            "api_key": "sk-xxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "system_prompt": "You are a senior code reviewer. Focus on security issues and potential bugs."
        }
    }
}
```

### Prompt File

Place a markdown file in `~/.config/gitsight/prompts/` and reference it by filename:

```
~/.config/gitsight/prompts/my_reviewer.md
```

```json
{
    "llms": {
        "openai": {
            "api_key": "sk-xxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "system_prompt_file": "my_reviewer.md"
        }
    }
}
```

**Priority**: `system_prompt_file` > `system_prompt` > built-in default prompt.

## Managing Config via CLI

```bash
# View config path
gs config

# Save GitHub token
gs config --set-token ghp_xxxxxxxxxxxx

# List available LLM providers
gs config --list-llms

# Set active LLM provider
gs config --set-llm local-ollama

# Use a different LLM for a single run
gs analyze owner/repo --llm openai

# View current config
cat ~/.config/gitsight/config.json
```
