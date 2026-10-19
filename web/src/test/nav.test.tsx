import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createMemoryHistory, createRouter } from '@tanstack/react-router';
import { cleanup, render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';

import { routeTree } from '@/routes';
import { installFakeServer, type FakeServer } from '@/test/fakeServer';
import { warmRouteModules } from '@/test/warmRoutes';

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

let server: FakeServer;

beforeAll(async () => {
  await warmRouteModules('@/routes/index', '@/routes/settings');
}, 300_000);

beforeEach(() => {
  server = installFakeServer();
});
afterEach(() => {
  cleanup();
  server.restore();
});

const NAV_ROUTES = ['/', '/analyze', '/compare', '/batch', '/subscriptions', '/usage', '/llm', '/settings', '/completion'];

describe('navigation', () => {
  it('offers a tab for every page, including settings', async () => {
    renderAt('/');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    const hrefs = screen.getAllByRole('link').map((a) => a.getAttribute('href'));
    for (const route of NAV_ROUTES) {
      expect(hrefs).toContain(route);
    }
  });

  it('reaches the settings page from the nav', async () => {
    const user = userEvent.setup();
    const { router } = renderAt('/');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    await user.click(screen.getByRole('link', { name: /^Settings$/ }));

    await waitFor(() => expect(router.state.location.pathname).toBe('/settings'));
    // The page arrives through a lazy chunk, so re-query inside the wait: a node
    // captured while Suspense is still swapping is already detached.
    await waitFor(() => {
      expect(screen.getByLabelText(/default window/i)).toBeInTheDocument();
    });
  });

  it('does not offer a second settings shortcut on the overview', async () => {
    renderAt('/');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    const links = screen.getAllByRole('link').filter((a) => a.getAttribute('href') === '/settings');
    // Exactly the one nav entry.
    expect(links).toHaveLength(1);
  });
});
