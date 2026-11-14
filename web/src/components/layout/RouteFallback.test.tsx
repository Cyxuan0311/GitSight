import {
  createMemoryHistory,
  createRootRoute,
  createRoute,
  createRouter,
  Outlet,
  RouterProvider,
} from '@tanstack/react-router';
import { describe, expect, it } from 'vitest';

import { RouteFallback } from './RouteFallback';
import { NAV_GROUPS } from './nav';
import i18n from '@/i18n';
import { renderWithProviders as render, screen } from '@/test/setup';

const NAV_ROUTES = NAV_GROUPS.flatMap((group) => group.items.map((item) => item.to));

/**
 * The fallback only needs enough of a route tree for its <Link>s to resolve;
 * every page is replaced by the fallback itself, so the test renders it the way
 * the router does while a page chunk is missing.
 */
function renderFallbackAt(path: string) {
  const root = createRootRoute({ component: () => <Outlet /> });
  const routeTree = root.addChildren(
    NAV_ROUTES.map((to) =>
      createRoute({ getParentRoute: () => root, path: to, component: RouteFallback }),
    ),
  );
  const router = createRouter({
    routeTree,
    history: createMemoryHistory({ initialEntries: [path] }),
  });
  return render(<RouterProvider router={router as never} />);
}

describe('RouteFallback', () => {
  it('keeps the shell and its navigation while a page chunk loads', async () => {
    renderFallbackAt('/usage');

    expect(await screen.findByRole('status')).toHaveTextContent(i18n.t('app.loading'));

    // Each tab is still reachable, so a slow chunk does not trap the reader in
    // the loading view.
    for (const route of NAV_ROUTES) {
      expect(screen.getAllByRole('link').some((link) => link.getAttribute('href') === route)).toBe(
        true,
      );
    }
  });

  it('highlights the tab whose page is on its way', async () => {
    renderFallbackAt('/usage');

    expect(await screen.findByRole('link', { name: i18n.t('nav.usage') })).toHaveAttribute(
      'aria-current',
      'page',
    );
    expect(screen.getByRole('link', { name: i18n.t('nav.analyze') })).not.toHaveAttribute(
      'aria-current',
    );
  });
});
