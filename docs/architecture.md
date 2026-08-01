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
 |    +-- github                    -- GitHub REST API via libcurl + cJSON
 |    +-- gitlab                    -- (planned)
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

### Adding GitLab support

1. Create `include/gitsight/platform/gitlab.h` and `src/platform/gitlab.c`
2. Implement the same 4 functions: `fetch_prs`, `fetch_commits`, `fetch_issues`, `destroy`
3. Register in `src/platform/client.c`:
   ```c
   ErrorCode platform_client_init(PlatformClient *client, const Config *cfg, ...) {
       if (/* is GitHub */)
           return github_client_init(client, cfg, owner, repo);
       else if (/* is GitLab */)
           return gitlab_client_init(client, cfg, owner, repo);
       ...
   }
   ```

The detection logic could be based on URL pattern (e.g., `gitlab.com` in the URL) or a `--platform` CLI flag.

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
