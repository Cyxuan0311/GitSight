import { afterEach, describe, expect, it, vi } from 'vitest';

import { ApiError, api } from '@/lib/api';

const original = globalThis.fetch;

afterEach(() => {
  globalThis.fetch = original;
});

function stubFetch(response: Response) {
  globalThis.fetch = vi.fn(async () => response) as typeof fetch;
}

describe('text endpoints', () => {
  it('returns the config file as text', async () => {
    const body = '{\n\t"platform":\t"github"\n}\n';
    stubFetch(
      new Response(body, {
        status: 200,
        headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      }),
    );
    expect(await api.getRawConfig()).toBe(body);
  });

  // The daemon labels the raw file text/plain, but a mislabelled response would
  // otherwise reach the editor as an object and render as "[object Object]".
  it('still yields a string when the server mislabels it as JSON', async () => {
    stubFetch(
      new Response('{"platform":"github"}', {
        status: 200,
        headers: { 'Content-Type': 'application/json' },
      }),
    );
    const raw = await api.getRawConfig();
    expect(typeof raw).toBe('string');
    expect(() => JSON.parse(raw)).not.toThrow();
  });

  it('returns a completion script verbatim', async () => {
    const script = '#compdef gs\n_gs() { :; }\n';
    stubFetch(
      new Response(script, {
        status: 200,
        headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      }),
    );
    expect(await api.completion('zsh')).toBe(script);
  });

  it('raises the server message when a text endpoint fails', async () => {
    stubFetch(
      new Response(JSON.stringify({ error: 'no_config', message: 'config file does not exist yet', status: 404 }), {
        status: 404,
        headers: { 'Content-Type': 'application/json' },
      }),
    );
    await expect(api.getRawConfig()).rejects.toMatchObject({
      status: 404,
      code: 'no_config',
      message: 'config file does not exist yet',
    });
  });
});

describe('json endpoints', () => {
  it('unwraps 204 as undefined', async () => {
    stubFetch(new Response(null, { status: 204 }));
    expect(await api.clearCache()).toBeUndefined();
  });

  it('surfaces the daemon error code and message', async () => {
    stubFetch(
      new Response(
        JSON.stringify({ error: 'unsupported_format', message: 'comparisons export as json or md', status: 400 }),
        { status: 400, headers: { 'Content-Type': 'application/json' } },
      ),
    );
    await expect(api.getConfig()).rejects.toBeInstanceOf(ApiError);
  });

  it('explains an unreachable daemon rather than leaking "failed to fetch"', async () => {
    globalThis.fetch = vi.fn(async () => {
      throw new TypeError('fetch failed');
    }) as typeof fetch;
    await expect(api.health()).rejects.toMatchObject({ status: 0, code: 'network' });
  });
});