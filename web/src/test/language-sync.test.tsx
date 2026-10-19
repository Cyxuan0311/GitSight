import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createMemoryHistory, createRouter } from '@tanstack/react-router';
import { act, cleanup, render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';

import { routeTree } from '@/routes';
import { installFakeServer, type FakeServer } from '@/test/fakeServer';
import { warmRouteModules } from '@/test/warmRoutes';

import i18n, { setLanguage } from '@/i18n';
import { useSyncLanguage } from '@/lib/language';

let server: FakeServer;

function renderAt(path: string) {
  const router = createRouter({ routeTree, history: createMemoryHistory({ initialEntries: [path] }) });
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false, gcTime: 0, staleTime: 0 } },
  });
  return {
    router,
    ...render(
      <QueryClientProvider client={client}>
        <RouterProvider router={router as never} />
      </QueryClientProvider>,
    ),
  };
}

beforeAll(async () => {
  await warmRouteModules('@/routes/index', '@/routes/settings');
}, 300_000);

beforeEach(() => {
  server = installFakeServer();
});
afterEach(() => {
  cleanup();
  server.restore();
  void setLanguage('en');
});

describe('one language, two places', () => {
  it('writes config.lang when the header toggle is used', async () => {
    const user = userEvent.setup();
    renderAt('/');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    await user.click(screen.getByRole('button', { name: '中' }));

    await waitFor(() => {
      expect(server.calls).toContain('PATCH /api/config');
    });
    // The daemon renders periods, summaries and exports from this value, so the
    // console switch has to move it or the two disagree.
    const patch = server.bodies.findIndex((body) => JSON.stringify(body).includes('lang'));
    expect(JSON.parse(server.bodies[patch] ?? '{}')).toMatchObject({ sets: { lang: 'zh' } });
  });

  it('changes the interface immediately, before the write lands', async () => {
    const user = userEvent.setup();
    renderAt('/');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });
    expect(i18n.language).toBe('en');

    await user.click(screen.getByRole('button', { name: '中' }));

    await waitFor(() => expect(i18n.language).toBe('zh'));
    await waitFor(() => expect(document.body.textContent).toContain('快速分析'));
  });

  it('keeps the console usable when the daemon cannot be reached', async () => {
    const user = userEvent.setup();
    const original = globalThis.fetch;
    renderAt('/');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    globalThis.fetch = vi
      .fn()
      .mockRejectedValue(new TypeError('fetch failed')) as unknown as typeof fetch;

    await user.click(screen.getByRole('button', { name: '中' }));
    await waitFor(() => expect(i18n.language).toBe('zh'));

    globalThis.fetch = original;
  });

  it('exposes the sync so a component can drive it', async () => {
    function Probe() {
      const sync = useSyncLanguage();
      return (
        <button type="button" onClick={() => sync('zh')}>
          go
        </button>
      );
    }
    const client = new QueryClient({ defaultOptions: { queries: { retry: false } } });
    render(
      <QueryClientProvider client={client}>
        <Probe />
      </QueryClientProvider>,
    );

    await act(async () => {
      screen.getByRole('button', { name: 'go' }).click();
    });
    await waitFor(() => expect(i18n.language).toBe('zh'));
    await waitFor(() => expect(server.calls).toContain('PATCH /api/config'));
  });
});