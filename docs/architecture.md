# Architecture

## Overview

GitSight is a CLI tool written in C17 that fetches GitHub repository activity data and generates structured reports, optionally enhanced by LLM-powered analysis.

## Module Layout

```
gs
 +-- cli (getopt_long)              -- command-line argument parsing
 +-- config (cJSON)                 -- config file read/write/merge
 +-- color (ANSI + isatty)          -- color output and terminal detection
 +-- anim (pthread)                 -- decoupled loading animations:
 |                                   line, braille dots/fill, bounce,
 |                                   unicode, triangles, quadrants,
 |                                   and a determinate progress bar
 +-- chart                          -- decoupled unicode bar-chart renderer
 |                                   (horizontal + vertical columns)
 +-- usage                          -- local LLM usage log: record, load,
 |                                   aggregate by provider or by day
 +-- platform/                      -- platform abstraction layer
 |    +-- client                    -- abstract interface (function pointer table)
 |    +-- profile                   -- per-platform settings table (auth scheme + default API base)
 |    +-- github                    -- shared "GitHub-compatible" driver used by
 |    |                               github / codeberg / gitea / forgejo
 |    +-- gitlab                    -- GitLab REST API via libcurl + cJSON
 +-- llm/                           -- LLM integration
 |    +-- client                    -- abstract LLM client interface
 |    +-- openai                    -- OpenAI-compatible API via libcurl + cJSON
 +-- analyzer/engine                -- statistics + LLM prompt building
 +-- export/                        -- report exporters
 |    +-- json                      -- via cJSON
 |    +-- md                        -- Markdown
 |    +-- txt                       -- ASCII table
 +-- container (dynamic vector)     -- generic dynamic array
 +-- models                         -- data structures (PR, Commit, Issue, Report)
 +-- error                          -- error codes and descriptions
```

## Data Flow

```
User Input
    |
    v
cli_parse() --> Config --> PlatformClient
                              |
                    +---------+---------+---------+
                    |         |         |         |
                    v         v         v         v
              fetch_prs  fetch_commits fetch_issues
                    |         |         |
                    +----+----+---------+
                         |
                         v
                   analyzer_run()
                    /            \
               statistics      [LLM optional]
                    |            |
                    +------+-----+
                           |
                           v
                     Report struct
                           |
                           v
                    Exporter (json/md/txt)
                           |
                           v
                     stdout / file
```

## Extending for New Platforms

The platform abstraction is based on a function pointer table (`PlatformClient`).

### GitHub-compatible hosts (Codeberg, Gitea, Forgejo, ...)

These hosts expose the same REST surface as GitHub
(`/repos/{owner}/{repo}/pulls|issues|commits|releases|contributors`). Adding
one only requires a new row in `src/platform/profile.c`:

```c
{ "mynamespace", "https://host.example.com/api/v1", "token" },
```

plus the platform name in `config.c` (`set_top_level` validation), the host
auto-detection in `src/main.c`, the CLI help text, and the shell completions.

### Structurally different platforms (e.g. GitLab)

1. Create `include/gitsight/platform/<name>.h` and `src/platform/<name>.c`
2. Implement the same 6 functions: `fetch_prs`, `fetch_commits`, `fetch_issues`,
   `fetch_releases`, `fetch_meta`, `fetch_contributors`, `destroy`
3. Register in `src/platform/client.c`:
   ```c
   ErrorCode platform_client_init(PlatformClient *client, const Config *cfg, ...) {
       if (strcmp(cfg->platform, "gitlab") == 0)
           return gitlab_client_init(client, cfg, owner, repo);
       return github_client_init(client, cfg, owner, repo);
   }
   ```
4. Update `config.c` (token routing + platform validation), host auto-detection
   in `src/main.c`, CLI help text, and the shell completions.

The detection logic is based on URL host patterns (e.g. `gitlab.com` → GitLab,
`codeberg.org` → Codeberg) or an explicit `--platform` CLI flag. Note that
pagination is generic: the `Link` header (GitHub/Gitea/Forgejo) and the
`X-Next-Page` header (GitLab) are both handled by `http_get_paginated()`.

### Adding a new LLM provider

1. Implement the `llm_generate()` signature from `include/gitsight/llm/client.h`
2. Add a provider selection in the LLM config (e.g., `"provider": "openai"` / `"provider": "anthropic"`)
3. Route calls to the appropriate implementation

## Key Design Decisions

- **Function pointer tables** (not inheritance) for platform abstraction -- keeps C code simple and transparent
- **cJSON bundled in-tree** -- avoids external dependency and network issues during build
- **libcurl for HTTP** -- battle-tested, supports all proxy types, widely available
- **Single binary output** -- static linking where possible, `gs` alone is enough
- **stderr for progress, stdout for data** -- enables clean piping without `--quiet` flags
