import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider, createMemoryHistory, createRouter } from '@tanstack/react-router';
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
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
  await warmRouteModules('@/routes/index', '@/routes/analyze', '@/routes/settings');
}, 300_000);

beforeEach(() => {
  server = installFakeServer();
});
afterEach(() => {
  cleanup();
  server.restore();
});

async function settled() {
  await waitFor(() => expect(server.calls.length).toBeGreaterThan(0), { timeout: 20_000 });
  await act(async () => {
    await new Promise((r) => setTimeout(r, 100));
  });
}

describe('overview quick start hands the repository to the analyze form', () => {
  it('puts the repo in the query string and prefills the form field', async () => {
    const user = userEvent.setup();
    const { router } = renderAt('/');
    await settled();

    await user.type(screen.getByPlaceholderText(/owner\/repo, or a full URL/i), 'acme/gadget');
    await user.click(screen.getByRole('button', { name: /^Analyze$/ }));

    await waitFor(() => {
      expect(router.state.location.pathname).toBe('/analyze');
    });
    // `hash` would leave search empty, and the form would come up blank.
    expect(router.state.location.search).toEqual({ repo: 'acme/gadget' });

    cleanup();
    renderAt('/analyze?repo=acme%2Fgadget');
    await settled();
    expect((screen.getByLabelText(/repository/i) as HTMLInputElement).value).toBe('acme/gadget');
  });
});

describe('analyze submission', () => {
  it('posts the repository that arrived in ?repo=', async () => {
    const user = userEvent.setup();
    server = installFakeServer();
    renderAt('/analyze?repo=acme%2Fgadget');
    await settled();

    await user.click(screen.getByRole('button', { name: /^Analyze$/ }));

    await waitFor(() => {
      expect(server.calls).toContain('POST /api/jobs/analyze');
    });
    expect(JSON.parse(server.bodies.at(-1) ?? '{}')).toMatchObject({ repo: 'acme/gadget' });
  });

  it('surfaces a server-side rejection instead of a blank report', async () => {
    const user = userEvent.setup();
    server = installFakeServer({
      routes: {
        '/api/jobs/analyze': {
          status: 400,
          body: {
            error: 'bad_request',
            message: "invalid repository 'x'; use owner/repo or a full URL",
            status: 400,
          },
        },
      },
    });
    renderAt('/analyze?repo=x');
    await settled();

    await user.click(screen.getByRole('button', { name: /^Analyze$/ }));

    const shown = await screen.findAllByText(/use owner\/repo or a full URL/i);
    expect(shown.length).toBeGreaterThan(0);
  });
});

describe('raw config editor', () => {
  it('shows the config file verbatim as editable JSON text', async () => {
    const user = userEvent.setup();
    renderAt('/settings');
    await settled();

    await user.click(screen.getByRole('button', { name: /raw file/i }));

    const textarea = await waitFor(() => {
      const node = document.querySelector('textarea');
      if (!node) throw new Error('the raw editor never opened');
      return node as HTMLTextAreaElement;
    });

    // A JSON content type here would make the client hand the editor an object.
    expect(textarea.value).not.toContain('[object Object]');
    expect(() => JSON.parse(textarea.value)).not.toThrow();
    expect(textarea.value).toContain('"default_days"');
  });

  it('saves what the editor contains', async () => {
    const user = userEvent.setup();
    renderAt('/settings');
    await settled();

    await user.click(screen.getByRole('button', { name: /raw file/i }));
    const textarea = await waitFor(() => {
      const node = document.querySelector('textarea');
      if (!node) throw new Error('the raw editor never opened');
      return node as HTMLTextAreaElement;
    });

    // userEvent.type() reads braces as key descriptors, so set the value directly.
    fireEvent.change(textarea, { target: { value: '{ "platform": "gitlab" }' } });
    await user.click(screen.getByRole('button', { name: /save raw/i }));

    await waitFor(() => {
      expect(server.calls).toContain('PUT /api/config/raw');
    });
    expect(JSON.parse(server.bodies.at(-1) ?? '')).toEqual({ platform: 'gitlab' });
  });

  it('reports a missing config file rather than showing an empty editor', async () => {
    const user = userEvent.setup();
    server = installFakeServer({
      routes: {
        '/api/config/raw': {
          status: 404,
          body: { error: 'no_config', message: 'config file does not exist yet', status: 404 },
        },
      },
    });
    renderAt('/settings');
    await settled();

    await user.click(screen.getByRole('button', { name: /raw file/i }));

    expect(await screen.findByText(/config file does not exist yet/i)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /save raw/i })).toBeDisabled();
  });
});

describe('navigation', () => {
  it('gives every nav group its own heading', async () => {
    const { container } = renderAt('/');
    await settled();

    // Two groups used to share nav.groupSystem, which both collided on a React
    // key and printed the same heading twice.
    const headings = Array.from(
      container.querySelectorAll('nav p.uppercase'),
      (node) => node.textContent,
    );
    expect(headings).toEqual(['Analysis', 'Manage', 'System']);
    expect(new Set(headings).size).toBe(headings.length);
  });
});