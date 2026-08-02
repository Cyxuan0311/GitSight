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
    "platform": "github",
    "github_token": "ghp_xxxxxxxxxxxx",
    "gitlab_token": "glpat_xxxxxxxxxxxx",
    "codeberg_token": "xxxxxxxxxxxxxxxxxxxx",
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
| `platform` | string | `"github"` | Platform: `"github"`, `"gitlab"`, `"codeberg"`, `"gitea"`, `"forgejo"` |
| `github_token` | string | `""` | GitHub personal access token |
| `gitlab_token` | string | `""` | GitLab personal access token |
| `codeberg_token` | string | `""` | Token for Gitea-family platforms (Codeberg / Gitea / Forgejo) |
| `api_base` | string | platform default | API base URL (per-platform default; override for mirrors/self-hosted) |
| `proxy` | string | `""` | HTTP/HTTPS/SOCKS5 proxy URL |
| `default_days` | number | `7` | Default analysis period in days |
| `color` | string | `"auto"` | Color mode: `"auto"`, `"always"`, `"never"` |
| `format` | string | `"md"` | Default output format: `"md"`, `"json"`, `"txt"`, `"xlsx"`, `"pdf"` |
| `active_llm` | string | `""` | Active LLM provider name (must match a key in `llms`) |
| `llms.<name>.api_key` | string | `""` | LLM API key |
| `llms.<name>.api_base` | string | `"https://api.openai.com/v1"` | LLM API base URL |
| `llms.<name>.protocol` | string | `"openai"` | LLM wire protocol: `"openai"`, `"anthropic"`, `"gemini"` (auto-detected from `api_base` when omitted) |
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

## Platforms

`platform` selects the API provider. GitHub and GitLab use dedicated drivers;
Codeberg, Gitea, and Forgejo share a single "GitHub-compatible" driver (they
expose the same REST surface and differ only in the authorization header
scheme and default API base).

| Platform | Default `api_base` | Auth header |
|----------|--------------------|-------------|
| `github`   | `https://api.github.com`       | `Authorization: Bearer <token>` |
| `gitlab`   | `https://gitlab.com/api/v4`    | `PRIVATE-TOKEN: <token>` |
| `codeberg` | `https://codeberg.org/api/v1`  | `Authorization: token <token>` |
| `gitea`    | `https://gitea.com/api/v1`     | `Authorization: token <token>` |
| `forgejo`  | `https://codeberg.org/api/v1`  | `Authorization: token <token>` |

When you analyze a URL, the platform is auto-detected from the host
(`gitlab.com` → GitLab, `codeberg.org` → Codeberg, hosts containing `gitea`
or `forgejo` → Gitea) and the matching `api_base` default is applied. Use
`--platform` or the config `platform` field to override, and `--api-base` /
`api_base` for self-hosted instances:

```bash
# Codeberg (auto-detected from the URL)
gs analyze https://codeberg.org/acme/gadget

# Self-hosted Gitea
gs analyze acme/gadget --platform gitea --api-base https://gitea.example.com/api/v1
```

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
| `GITLAB_TOKEN` | `gitlab_token` |
| `CODEBERG_TOKEN` / `GS_CODEBERG_TOKEN` | `codeberg_token` |
| `GS_API_BASE` | `api_base` |
| `HTTP_PROXY` | `proxy` |
| `HTTPS_PROXY` | `proxy` |
| `NO_COLOR` | `color` (disables color if set) |
| `LLM_API_KEY` | `active_llm.api_key` |
| `LLM_API_BASE` | `active_llm.api_base` |
| `LLM_MODEL` | `active_llm.model` |
| `LLM_PROTOCOL` | `active_llm.protocol` |

## Multiple LLM Providers

The config file supports any number of named LLM providers. Switch between them with
`gs config --set-llm <name>` or `gs analyze <repo> --llm <name>`.

Each provider declares a `protocol` that selects the wire format:

| Protocol | Providers | Default `api_base` | Request format |
|----------|-----------|--------------------|----------------|
| `openai` | OpenAI, Ollama, LM Studio, vLLM, most local servers | `https://api.openai.com/v1` | `/chat/completions`, `Authorization: Bearer` |
| `anthropic` | Anthropic Claude | `https://api.anthropic.com` | `/v1/messages`, `x-api-key` |
| `gemini` | Google Gemini | `https://generativelanguage.googleapis.com/v1beta` | `/models/{model}:generateContent`, `x-goog-api-key` |

`protocol` is optional: when omitted it is auto-detected from `api_base` (an
`anthropic.com`/`claude.ai` host selects `anthropic`, a
`generativelanguage.googleapis.com` host selects `gemini`, everything else
defaults to `openai`).

Example with three providers:

```json
{
    "active_llm": "openai",
    "llms": {
        "openai": {
            "api_key": "sk-xxxxxxxxxxxx",
            "api_base": "https://api.openai.com/v1",
            "model": "gpt-4o-mini"
        },
        "claude": {
            "protocol": "anthropic",
            "api_key": "sk-ant-xxxxxxxxxxxx",
            "api_base": "https://api.anthropic.com",
            "model": "claude-sonnet-4-20250514"
        },
        "gemini": {
            "protocol": "gemini",
            "api_key": "AIzaSyxxxxxxxxxxxx",
            "api_base": "https://generativelanguage.googleapis.com/v1beta",
            "model": "gemini-2.0-flash"
        },
        "local-ollama": {
            "api_key": "ollama",
            "api_base": "http://localhost:11434/v1",
            "model": "llama3.2"
        }
    }
}
```

> Note: with Ollama the `api_key` is ignored by the server but still required by
> GitSight; use any non-empty value such as `"ollama"`.

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

# Switch a provider to the Anthropic protocol
gs config --set llms.claude.protocol=anthropic

# Set a provider's model via the CLI
gs config --set llms.claude.model=claude-sonnet-4-20250514

# View current config
cat ~/.config/gitsight/config.json
```
