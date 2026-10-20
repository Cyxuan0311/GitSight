import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createMemoryHistory, createRouter } from '@tanstack/react-router';
import { cleanup, render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';

import { routeTree } from '@/routes';
import { warmRouteModules } from '@/test/warmRoutes';
import { installFakeServer, type FakeServer } from '@/test/fakeServer';

/**
 * The CLI's per-run flags are the same for every page that starts a job, so
 * the console has to carry them the same way. These tests drive the compare
 * and batch forms and read the request body back, which is the only place the
 * flags are actually decided.
 */
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

const originalEventSource = globalThis.EventSource;
let server: FakeServer;

beforeAll(async () => {
  await warmRouteModules('@/routes/compare', '@/routes/batch', '@/routes/index');
}, 300_000);

beforeEach(() => {
  globalThis.EventSource = class {
    readyState = 1;
    onerror: (() => void) | null = null;
    addEventListener() {}
    close() {}
  } as unknown as typeof EventSource;
  server = installFakeServer({
    routes: {
      '/api/jobs/compare': { status: 202, body: { id: 'job_c_1', kind: 'compare', state: 'queued' } },
      '/api/jobs/analyze': { status: 202, body: { id: 'job_a_1', kind: 'analyze', state: 'queued' } },
    },
  });
});

afterEach(() => {
  cleanup();
  server.restore();
});

afterAll(() => {
  globalThis.EventSource = originalEventSource;
});

/** The body of the last POST the page sent, parsed. */
function lastBody(): Record<string, unknown> {
  expect(server.bodies.length).toBeGreaterThan(0);
  return JSON.parse(server.bodies[server.bodies.length - 1]!) as Record<string, unknown>;
}

describe('per-run options on /compare', () => {
  it('sends the advanced flags alongside the comparison', async () => {
    const user = userEvent.setup();
    renderAt('/compare');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    await user.type(screen.getByPlaceholderText(/curl\/curl/), 'acme/gadget');
    await user.click(screen.getByRole('button', { name: 'Advanced options' }));

    await user.type(screen.getByLabelText('Branch'), 'main');
    await user.type(screen.getByLabelText('Filters'), 'author:alice');
    await user.type(screen.getByLabelText('Export overrides'), 'show_commits=false');
    await user.click(screen.getByRole('checkbox', { name: 'Bypass the response cache' }));

    await user.click(screen.getByRole('button', { name: 'Compare' }));

    await waitFor(() => expect(server.calls).toContain('POST /api/jobs/compare'), { timeout: 20_000 });
    const body = lastBody();
    expect(body.repo).toBe('acme/gadget');
    expect(body.branch).toBe('main');
    expect(body.filters).toEqual(['author:alice']);
    expect(body.export).toEqual(['show_commits=false']);
    expect(body.no_cache).toBe(true);
  });

  it('refuses to submit an override the exporter does not know', async () => {
    const user = userEvent.setup();
    renderAt('/compare');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    await user.type(screen.getByPlaceholderText(/curl\/curl/), 'acme/gadget');
    await user.click(screen.getByRole('button', { name: 'Advanced options' }));
    await user.type(screen.getByLabelText('Export overrides'), 'show_pull_request=false');

    await user.click(screen.getByRole('button', { name: 'Compare' }));

    expect(await screen.findByText(/Unknown key/)).toBeInTheDocument();
    expect(server.calls).not.toContain('POST /api/jobs/compare');
  });

  it('offers the export keys the server accepts', async () => {
    const user = userEvent.setup();
    renderAt('/compare');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    await user.click(screen.getByRole('button', { name: 'Advanced options' }));
    // The daemon lists the names its exporter table holds, so the console
    // cannot offer a key that would be silently ignored.
    expect(await screen.findByRole('button', { name: 'show_commits' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'pdf.margin' })).toBeInTheDocument();
  });
});

describe('per-run options on /batch', () => {
  it('sends the window and the advanced flags with the batch', async () => {
    const user = userEvent.setup();
    renderAt('/batch');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    await user.type(screen.getByPlaceholderText(/owner\/repo/), 'acme/gadget\nacme/other');
    await user.click(screen.getByRole('button', { name: 'Advanced options' }));
    await user.type(screen.getByLabelText('Export overrides'), 'item_limit=3');

    await user.click(screen.getByRole('button', { name: 'Run batch' }));

    await waitFor(() => expect(server.calls).toContain('POST /api/jobs/analyze'), { timeout: 20_000 });
    const body = lastBody();
    expect(body.repos).toEqual(['acme/gadget', 'acme/other']);
    expect(body.since_days).toBe(7);
    expect(body.export).toEqual(['item_limit=3']);
  });
});
