import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createMemoryHistory, createRouter } from '@tanstack/react-router';
import { act, cleanup, render, waitFor } from '@testing-library/react';
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';

import { routeTree } from '@/routes';
import { installFakeServer, type FakeServer } from '@/test/fakeServer';
import { warmRouteModules } from '@/test/warmRoutes';

import { setLanguage } from '@/i18n';

/**
 * One pass over every page, checked for everything that can only be seen with
 * the page actually mounted: React warnings, a failing request, and translation
 * keys that fall through to their raw name.
 *
 * This used to be two suites — one for console errors, one for missing keys —
 * each walking all nine pages. They assert on the same render, so walking twice
 * bought nothing and doubled the most expensive part of the test run.
 */
const PAGES = [
  '/',
  '/analyze',
  '/compare',
  '/batch',
  '/subscriptions',
  '/usage',
  '/llm',
  '/settings',
  '/completion',
];

function renderAt(path: string) {
  const router = createRouter({ routeTree, history: createMemoryHistory({ initialEntries: [path] }) });
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false, gcTime: 0, staleTime: 0 } },
  });
  return render(
    <QueryClientProvider client={client}>
      <RouterProvider router={router as never} />
    </QueryClientProvider>,
  );
}

async function settle(path: string): Promise<string> {
  await act(async () => {
    renderAt(path);
  });
  await waitFor(() => expect(document.body.textContent?.length ?? 0).toBeGreaterThan(0), {
    timeout: 20_000,
  });
  // Let the mocked queries land.
  await act(async () => {
    await new Promise((resolve) => setTimeout(resolve, 150));
  });
  return document.body.textContent ?? '';
}

let server: FakeServer;

// Vite transforms recharts and the report bundle in tens of seconds on a cold
// cache, so the page modules are pulled in before any test starts rather than
// inside a per-test timeout through the router's lazy boundary.
beforeAll(async () => {
  await warmRouteModules('@/routes/index', '@/routes/analyze', '@/routes/compare', '@/routes/batch', '@/routes/subscriptions', '@/routes/usage', '@/routes/llm', '@/routes/settings', '@/routes/completion');
  void setLanguage('zh');
}, 300_000);

beforeEach(() => {
  server = installFakeServer();
});
afterEach(() => {
  cleanup();
  server.restore();
});
afterAll(() => {
  void setLanguage('en');
});

describe('every console page mounts cleanly and fully translated', () => {
  for (const path of PAGES) {
    it(
      `renders ${path} without console errors or untranslated keys`,
      { timeout: 120_000 },
      async () => {
        const errors: string[] = [];
        const spy = vi.spyOn(console, 'error').mockImplementation((...args) => {
          errors.push(args.map(String).join(' '));
        });

        const text = await settle(path);

        spy.mockRestore();

        // React logs a duplicate-key warning once per page mount when the nav
        // groups collide, so the render suite asserts on the absence of *any*
        // console error rather than only crashing ones.
        expect(errors.filter((m) => !m.includes('not wrapped in act'))).toEqual([]);
        expect(server.calls.some((c) => c.startsWith('GET /api/'))).toBe(true);

        // i18next renders "⚠ <key>" when a lookup misses; that marker is the tell.
        expect([...text.matchAll(/⚠\s*[\w.]+/g)].map((m) => `${path}: ${m[0]}`)).toEqual([]);
      },
    );
  }
});

describe('switching language re-renders in place', () => {
  it('replaces the visible text without a reload', async () => {
    void setLanguage('en');
    const english = await settle('/');
    cleanup();

    expect(english).toContain('Quick analysis');

    void setLanguage('zh');
    const chinese = await settle('/');
    cleanup();

    expect(chinese).toContain('快速分析');
    expect(chinese).not.toContain('Quick analysis');
    expect(document.documentElement.lang).toBe('zh');
  });
});