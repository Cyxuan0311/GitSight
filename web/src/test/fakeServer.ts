/**
 * A fake `gs serve` backed by payloads captured from a real daemon, so a page
 * test renders exactly the shapes the server produces rather than a hand-written
 * approximation that can drift from it.
 *
 * Regenerate the fixture files with the script noted in the repository's test
 * suite when an endpoint's response shape changes.
 */
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { vi } from 'vitest';

const here = dirname(fileURLToPath(import.meta.url));

function load<T>(name: string): T {
  return JSON.parse(readFileSync(join(here, 'fixtures', name), 'utf8')) as T;
}

function loadText(name: string): string {
  return readFileSync(join(here, 'fixtures', name), 'utf8');
}

export interface ServerFixture {
  health: unknown;
  options: unknown;
  quota: unknown;
  cache: unknown;
  jobs: unknown;
  llms: unknown;
  config: unknown;
  subscriptions: unknown;
  usage: unknown;
  usageRecords: unknown;
  report: unknown;
  compare: unknown;
  batch: unknown;
  configRaw: string;
  completion: string;
}

export function loadFixtures(): ServerFixture {
  return {
    health: load('health.json'),
    options: load('meta-options.json'),
    quota: load('quota.json'),
    cache: load('cache.json'),
    jobs: load('jobs.json'),
    llms: load('llms.json'),
    config: load('config.json'),
    subscriptions: load('subscriptions.json'),
    usage: load('usage.json'),
    usageRecords: load('usage-records.json'),
    report: load('report.json'),
    compare: load('compare.json'),
    batch: load('batch.json'),
    configRaw: loadText('config-raw.json'),
    completion: loadText('completion.txt'),
  };
}

/** A response the fake server should produce instead of the fixture table. */
export interface StubResponse {
  status?: number;
  body?: unknown;
  text?: string;
  contentType?: string;
}

export type RouteOverride = StubResponse | (() => StubResponse | Promise<StubResponse>);

export interface FakeServer {
  /** Requests seen so far, as "METHOD /path". */
  calls: string[];
  /** Bodies of POST/PATCH/PUT requests, in order. */
  bodies: string[];
  /** The installed stub, so a test can intercept selected paths itself. */
  fetch: typeof globalThis.fetch;
  restore: () => void;
}

export interface FakeServerOptions {
  /** Keyed by pathname with the query string stripped. */
  routes?: Record<string, RouteOverride>;
}

/**
 * Installs a `fetch` stub over the fixture table. Paths the table does not
 * cover fall through to a per-endpoint default, so a page renders without
 * enumerating every endpoint it happens to touch.
 */
export function installFakeServer(options: FakeServerOptions = {}): FakeServer {
  const f = loadFixtures();
  const calls: string[] = [];
  const bodies: string[] = [];
  const original = globalThis.fetch;

  const json: Record<string, unknown> = {
    '/api/health': f.health,
    '/api/meta/options': f.options,
    '/api/quota': f.quota,
    '/api/cache': f.cache,
    '/api/jobs': f.jobs,
    '/api/llms': f.llms,
    '/api/config': f.config,
    '/api/subscriptions': f.subscriptions,
    '/api/usage': f.usage,
    '/api/usage/records': f.usageRecords,
  };

  const text: Record<string, string> = {
    '/api/config/raw': f.configRaw,
  };

  const installed = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
    const raw = typeof input === 'string' ? input : input.toString();
    const method = init?.method ?? 'GET';
    calls.push(`${method} ${raw}`);
    if (typeof init?.body === 'string') bodies.push(init.body);

    const path = raw.split('?')[0] ?? raw;

    const override = options.routes?.[path];
    if (override) {
      const stub = typeof override === 'function' ? await override() : override;
      return reply(stub.status ?? 200, stub);
    }
    if (path in json) return reply(200, { body: json[path] });
    if (path in text) {
      return new Response(text[path], {
        status: 200,
        headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      });
    }
    if (path.startsWith('/api/jobs/') && path.endsWith('/result')) {
      return reply(200, { body: f.report });
    }
    if (path.startsWith('/api/jobs/') && path.endsWith('/export')) {
      return reply(200, { body: new Uint8Array([1, 2, 3]) });
    }
    if (path.startsWith('/api/completion/')) {
      return new Response(f.completion, {
        status: 200,
        headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      });
    }
    if (path.startsWith('/api/subscriptions/')) return reply(200, { body: f.subscriptions });
    return reply(404, {
      body: { error: 'not_found', message: `no fixture for ${path}`, status: 404 },
    });
  }) as unknown as typeof fetch;

  globalThis.fetch = installed;

  return {
    calls,
    bodies,
    fetch: installed,
    restore: () => {
      globalThis.fetch = original;
    },
  };
}

function reply(status: number, stub: StubResponse): Response {
  if (stub.text !== undefined) {
    return new Response(stub.text, {
      status,
      headers: { 'Content-Type': stub.contentType ?? 'text/plain; charset=utf-8' },
    });
  }
  return new Response(JSON.stringify(stub.body ?? null), {
    status,
    headers: { 'Content-Type': stub.contentType ?? 'application/json; charset=utf-8' },
  });
}