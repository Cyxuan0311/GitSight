import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createMemoryHistory, createRouter } from '@tanstack/react-router';
import { act, cleanup, render, waitFor } from '@testing-library/react';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';

import { routeTree } from '@/routes';
import { installFakeServer, type FakeServer } from '@/test/fakeServer';
import { warmRouteModules } from '@/test/warmRoutes';

import { setLanguage } from '@/i18n';

/**
 * The usage dashboard is where compact units are most visible, and where a
 * locale leak is most likely to go unnoticed: 万 sits next to numbers that look
 * otherwise ordinary. The page is asserted directly rather than through the
 * formatter, because the point is what a reader ends up looking at.
 */
const CJK = /[一-鿿]/;

function renderAt(path: string) {
  const router = createRouter({ routeTree, history: createMemoryHistory({ initialEntries: [path] }) });
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false, gcTime: 0 } },
  });
  return render(
    <QueryClientProvider client={client}>
      <RouterProvider router={router as never} />
    </QueryClientProvider>,
  );
}

let server: FakeServer;

beforeAll(async () => {
  await warmRouteModules('@/routes/usage');
}, 300_000);

beforeEach(() => {
  server = installFakeServer();
});
afterEach(() => {
  cleanup();
  server.restore();
  void setLanguage('en');
});

/**
 * The page body without the shell around it: the header carries the language
 * toggle, whose 中 is a control rather than a translation.
 */
async function usagePage(): Promise<string> {
  await act(async () => {
    renderAt('/usage');
  });
  await waitFor(() => expect(document.body.textContent?.length ?? 0).toBeGreaterThan(0), {
    timeout: 20_000,
  });
  await act(async () => {
    await new Promise((resolve) => setTimeout(resolve, 200));
  });
  return document.querySelector('main')?.textContent ?? '';
}

describe('usage units', () => {
  it('abbreviates in English when the console is English', async () => {
    await setLanguage('en');
    const text = await usagePage();

    // The fixtures carry more than ten thousand tokens, so the total is compact.
    expect(text).toMatch(/\d+(\.\d+)?K/);
    expect(text).not.toMatch(/[万亿]/);
    expect(text).not.toMatch(CJK);
  });

  it('uses Chinese units when the console is Chinese', async () => {
    await setLanguage('zh');
    const text = await usagePage();
    expect(text).toMatch(/[万亿]/);
  });
});