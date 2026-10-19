import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createMemoryHistory, createRouter } from '@tanstack/react-router';
import { act, cleanup, render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';

import { routeTree } from '@/routes';
import { warmRouteModules } from '@/test/warmRoutes';
import { installFakeServer, loadFixtures, type FakeServer } from '@/test/fakeServer';

/**
 * `useJob` returns a fresh state holder per call, so a page that renders progress
 * and results from one instance while the form submits through another will never
 * see anything. These tests drive the real submission path and assert that the
 * run is visible, which is what a user sees after clicking Analyze.
 */
const fixtures = loadFixtures();

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

/** The minimal EventSource the browser would give the progress stream. */
class FakeEventSource {
  static last: FakeEventSource | null = null;
  readyState = 1;
  onerror: (() => void) | null = null;
  private listeners = new Map<string, ((event: MessageEvent) => void)[]>();
  closed = false;

  constructor(readonly url: string) {
    FakeEventSource.last = this;
  }

  addEventListener(type: string, fn: (event: MessageEvent) => void) {
    const list = this.listeners.get(type) ?? [];
    list.push(fn);
    this.listeners.set(type, list);
  }

  close() {
    this.closed = true;
  }

  emit(type: string, data: unknown) {
    for (const fn of this.listeners.get(type) ?? [])
      fn({ data: JSON.stringify(data) } as MessageEvent);
  }
}

const originalEventSource = globalThis.EventSource;
let server: FakeServer;

beforeAll(async () => {
  await warmRouteModules('@/routes/analyze', '@/routes/subscriptions', '@/routes/index');
}, 300_000);

// Installed per test rather than once: jsdom has no EventSource at all, so
// restoring the "original" between tests would leave nothing to construct.
beforeEach(() => {
  FakeEventSource.last = null;
  globalThis.EventSource = FakeEventSource as unknown as typeof EventSource;
  server = installFakeServer({
    routes: { '/api/jobs/analyze': { status: 202, body: { id: 'job_1_1', kind: 'analyze', state: 'queued' } } },
  });
});
afterEach(() => {
  cleanup();
  server.restore();
});

afterAll(() => {
  globalThis.EventSource = originalEventSource;
});

async function submitAndStream(repo: string) {
  const user = userEvent.setup();
  renderAt(`/analyze?repo=${encodeURIComponent(repo)}`);
  await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

  await user.click(screen.getByRole('button', { name: /^Analyze$/ }));

  await waitFor(() => expect(FakeEventSource.last).not.toBeNull(), { timeout: 10_000 });
  const source = FakeEventSource.last!;

  await act(async () => {
    source.emit('hello', { id: 'job_1_1', state: 'queued' });
    source.emit('progress', { stage: 'acme/gadget - Fetching', current: 1, total: 4, state: 0 });
  });
  return { user, source };
}

describe('a repository analysis on /analyze', () => {
  it('shows progress while the job runs', async () => {
    await submitAndStream('acme/gadget');
    // The page's own AnalysisProgress must reflect the stream the form opened.
    expect(await screen.findByText(/Fetching/)).toBeInTheDocument();
  });

  it('renders the report once the job completes', { timeout: 60_000 }, async () => {
    const { source } = await submitAndStream('acme/gadget');

    await act(async () => {
      source.emit('done', { id: 'job_1_1', state: 'done' });
    });

    const report = fixtures.report as { owner: string; repo: string };
    // The report heading carries "owner/repo"; the title also appears elsewhere
    // once a run is in flight, so match the heading specifically.
    await waitFor(() => {
      const heading = screen
        .getAllByText(new RegExp(`${report.owner}/${report.repo}`))
        .some((node) => node.tagName === 'SPAN' && node.closest('h3, [class*="truncate"]'));
      expect(heading).toBe(true);
    });
    // A pull-request row only exists once the report is on screen.
    expect(await screen.findByText('Fix the widget')).toBeInTheDocument();
    // The finished report carries the export preview, so every format the daemon
    // renders can be inspected without leaving the page.
    expect(await screen.findByRole('tab', { name: /^PDF$/ })).toBeInTheDocument();
    expect(await screen.findByRole('tab', { name: /^Report$/ })).toBeInTheDocument();
  });

  it('records the job so it shows up in the daemon history', async () => {
    await submitAndStream('acme/gadget');
    expect(server.calls).toContain('POST /api/jobs/analyze');
  });
});

describe('resuming a run from the URL', () => {
  it('adopts ?job= and tails that job instead of starting a new one', async () => {
    renderAt('/analyze?job=job_9_9');
    await waitFor(() => expect(FakeEventSource.last).not.toBeNull(), { timeout: 10_000 });

    // No form submission happened, yet the stream is already attached.
    expect(server.calls).not.toContain('POST /api/jobs/analyze');
    expect(FakeEventSource.last!.url).toContain('/api/jobs/job_9_9/events');

    await act(async () => {
      FakeEventSource.last!.emit('progress', {
        stage: 'acme/gadget - Fetching',
        current: 2,
        total: 5,
        state: 0,
      });
    });
    expect(await screen.findByText(/Fetching/)).toBeInTheDocument();
  });
});

describe('overview job history', () => {
  it('links each analysis back to the page that renders it', async () => {
    renderAt('/');
    await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });

    const jobs = (fixtures.jobs as { jobs: { id: string; title: string }[] }).jobs;
    const first = jobs[0]!;
    const link = await screen.findByRole('link', { name: first.title || first.id });
    expect(link).toHaveAttribute('href', `/analyze?job=${encodeURIComponent(first.id)}`);
  });
});