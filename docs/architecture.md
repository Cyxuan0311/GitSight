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

## The Web Console

`gs serve` runs an HTTP daemon that drives the exact code paths the CLI already
uses. There is no second implementation of the analysis, export, or
configuration logic: the server is a transport layer in front of the same
modules.

### How the seam is kept thin

Four refactors made the existing code reusable without changing its behaviour:

| Refactor | Why |
|---|---|
| `AnalysisResult` in `src/cmd.h` | `analyze_repo()` previously freed the item vectors before returning, so a caller could only get the aggregate `Report`. It now hands back the `Report` plus the six `Vector`s, which is exactly what `Exporter.write_report` needs. |
| `analysis_render()` | Uses `open_memstream()` to back the exporter's `FILE*` with a heap buffer. All seven formats — including the binary `xlsx` and `pdf` — therefore take one code path and never touch the filesystem. |
| `anim_set_listener()` | Every stage transition already passes through `anim_start`/`anim_update`/`anim_stop`. Installing a listener turns the animation into a structured progress stream with no added instrumentation. The CLI leaves it unset. |
| `cancel.h` with a thread-local binding | `CURLOPT_XFERINFOFUNCTION` aborts in-flight transfers. The flag is bound per worker thread so cancelling one job cannot abort the repositories running beside it. |

`Options` already holds every analyze flag the CLI accepts, so a request body
deserialises straight into it and `analyze_repo()` is called unchanged.

### Module layout

```
gs serve
 +-- server/
 |    +-- net.c                     -- POSIX sockets, HTTP/1.1 request parsing,
 |    |                               keep-alive, binary-safe responses
 |    +-- router.c (respond.c)      -- method + ":param" route matching, cJSON
 |    |                               response helpers, ErrorCode -> HTTP status
 |    +-- static_files.c            -- serves web/dist, SPA fallback, MIME map
 |    +-- jobs.c                    -- job registry, worker threads, progress log,
 |    |                               SSE subscriber list
 |    +-- serve.c                   -- lifecycle, config/usage path resolution,
 |    |                               the global state lock
 |    +-- api_meta.c                -- /api/health, /api/meta/options, /api/quota
 |    +-- api_config.c              -- config get/patch/raw/reset (secrets redacted)
 |    +-- api_subs.c                -- gs watch add/remove/update/list/analyze
 |    +-- api_llm.c                 -- provider CRUD, activation, connectivity test
 |    +-- api_usage.c               -- usage aggregates, records, clear, completion
 |    +-- api_jobs.c                -- job submission, result, export, SSE, cancel
 |    +-- api_util.c                -- request body -> Options translation
```

### Concurrency model

`gs serve` is single-user and binds to loopback by default, so the model is
deliberately conservative:

- **One analysis at a time.** A global mutex serialises anything touching
  process-global state the analysis path uses: the language global, the usage
  log, the HTTP response cache, and the config file. The work is network bound,
  so a single writer keeps all of those consistent without threading new locks
  through the existing modules.
- **Cross-repository parallelism.** Within one job, repositories run on up to
  `batch_max_threads` threads. Each repository fetches its seven resources
  *serially*, so a batch of N repositories costs N threads rather than N*7.
- **Per-job cancellation.** The cancel flag is bound to the calling thread, so
  aborting one repository cannot interrupt its neighbours.
- **Per-connection threads.** SSE streams need to block for minutes, so each
  connection gets its own thread rather than sharing a selector loop.

### Jobs and exports

A finished job owns its `AnalysisResult`. That is what makes
`GET /api/jobs/:id/export?format=pdf` possible: any of the seven formats is
rendered from memory on demand, without refetching and without the
`xlsx`/`pdf`-require-`-o` restriction the CLI enforces for stdout. The exporter
settings are snapshotted when the job runs, so changing a setting later cannot
retroactively alter an artifact.

The SSE handler owns the terminal frame rather than `job_finish()`, so a
browser that connects after the work already finished still receives exactly
one `done` event.

### Adding an endpoint

1. Write the handler in the matching `api_*.c`, taking `(Request*, Response*)`
   and using the `res_*` helpers. Read the config through `server_load_config()`
   so it is copied under the state lock.
2. Declare it in `api_register_*()` with `router_add()`.
3. Add a curl assertion to `tests/test_server.sh`, which runs the whole API
   offline against `GS_FIXTURE_DIR`.
