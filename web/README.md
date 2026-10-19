# GitSight Web Console

English | [中文](README.zh-CN.md)

The browser UI served by `gs serve`. It is a thin front-end over the same
analysis, export and configuration engine as the CLI — it reimplements nothing,
and it reads and writes the same `~/.config/gitsight/config.json`.

```bash
gs serve                  # → http://127.0.0.1:8787
```

## Pages

| Page | What it covers |
|---|---|
| **Overview** | Server status, API quota, response cache, recent jobs |
| **Analyze** | Every `gs analyze` option, live progress, interactive report, all 7 downloads |
| **Compare** | Two windows side by side, with per-metric deltas and a trend summary |
| **Batch** | Up to 16 repositories at once, 8 in parallel |
| **Subscriptions** | `gs watch` CRUD, plus one-click and batch analysis |
| **LLM usage** | Token spend per day and per provider, raw record log |
| **LLM providers** | Provider CRUD, protocol selection, connectivity test |
| **Settings** | Every `gs config --set` key, including the 33 export options |
| **Shell completion** | The embedded bash/zsh/fish scripts |

## What the console adds

Two things the console can do that a one-shot CLI call cannot:

- **Live progress.** A run streams to the browser over SSE and can be cancelled
  mid-flight.
- **Re-render, not refetch.** A finished analysis stays in memory, so switching
  export format re-renders the stored result instead of contacting the platform
  again.

## Getting started

### Build

```bash
# Build the console
npm --prefix web install
npm --prefix web run build
cp -r web/dist build/web

# Or in one step (needs Node):
cmake -B build -DGITSIGHT_BUILD_WEB=ON && cmake --build build
```

With `GITSIGHT_BUILD_WEB=ON` the daemon serves the console from the embedded
`build/web` directory, so the single binary needs no separate web server.

### Development

```bash
# API on :8787, Vite on :5173 proxying /api
gs serve --port 8787 &
npm --prefix web run dev
```

Vite proxies `/api` to the daemon, so the browser only ever talks to one origin
and relative API paths, cookies and the SSE stream behave exactly as they do in
production.

### Security

`gs serve` binds to loopback and has **no authentication** — it reads and writes
the config file holding your access tokens. Only pass `--host 0.0.0.0` on a
network you trust.

## Tech stack

- **React 19** with **TypeScript 5.7**, bundled by **Vite 6**
- **TanStack Router** for routing, **TanStack Query** for server state
- **Tailwind CSS 4** for styling, **Base UI** for accessible primitives
- **Recharts** for charts, **i18next** for the English/Chinese UI
- **Vitest** + **Testing Library** for tests

Node 20 or newer is required to build.

## Project layout

```
web/
├── src/
│   ├── routes/        # one file per page
│   ├── features/      # page-level components (analyze, settings, …)
│   ├── components/    # ui/ primitives, layout/, report/
│   ├── lib/           # api client, formatters, parsers, types
│   ├── i18n/          # translation setup and en/zh tables
│   ├── hooks/         # shared hooks (e.g. useJob)
│   ├── stores/        # zustand UI state
│   └── test/          # integration tests and fixtures
└── vitest.config.ts
```

## Testing

```bash
npm --prefix web run test        # single run
npm --prefix web run test:watch  # watch mode
npm --prefix web run typecheck
npm --prefix web run lint
```

Integration tests live in `src/test` and mount real pages against a mocked HTTP
server (`src/test/fakeServer.ts`); unit tests sit next to the code they cover.

> The suite is I/O-bound. Module loading of `recharts` alone dominates a cold
> run, and on a slow mount (for example a WSL `/mnt/...` drive) the Vitest
> worker can time out mid-transform. Keeping the repository on a native
> filesystem avoids it.

## Related documentation

- HTTP API reference: [docs/usage.md](../docs/usage.md#http-api)
- Configuration keys: [docs/configuration.md](../docs/configuration.md)
- Architecture: [docs/architecture.md](../docs/architecture.md)
- Project overview: [README.md](../README.md)
