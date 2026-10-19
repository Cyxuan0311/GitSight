import { createRootRoute, createRoute, Outlet } from '@tanstack/react-router';
import { Suspense, lazy } from 'react';

/**
 * A flat route tree built in code rather than generated. The console has nine
 * pages and no dynamic segments, so a hand-written tree stays readable and
 * keeps the file-based codegen step out of the build.
 *
 * Pages are split into separate chunks because the report and usage views pull
 * in charting code that the overview never needs.
 *
 * Each route is declared inline rather than through a helper so TypeScript
 * keeps the literal path type, which is what gives navigate() and <Link> their
 * route checking.
 */
const rootRoute = createRootRoute({
  component: () => <Outlet />,
  notFoundComponent: () => (
    <div className="grid min-h-screen place-items-center text-sm text-muted">
      <p>404 — not found</p>
    </div>
  ),
});

/** Wraps a lazy page in Suspense; the router wants a component, not an element. */
const withSuspense = (Component: React.ComponentType): React.FC => () => (
  <Suspense fallback={<div className="p-6 text-sm text-muted">Loading…</div>}>
    <Component />
  </Suspense>
);

const OverviewPage = lazy(() =>
  import('./routes/index').then((m) => ({ default: m.OverviewPage })),
);
const AnalyzePage = lazy(() =>
  import('./routes/analyze').then((m) => ({ default: m.AnalyzePage })),
);
const ComparePage = lazy(() =>
  import('./routes/compare').then((m) => ({ default: m.ComparePage })),
);
const BatchPage = lazy(() =>
  import('./routes/batch').then((m) => ({ default: m.BatchPage })),
);
const SubscriptionsPage = lazy(() =>
  import('./routes/subscriptions').then((m) => ({ default: m.SubscriptionsPage })),
);
const UsagePage = lazy(() =>
  import('./routes/usage').then((m) => ({ default: m.UsagePage })),
);
const LlmPage = lazy(() => import('./routes/llm').then((m) => ({ default: m.LlmPage })));
const SettingsPage = lazy(() =>
  import('./routes/settings').then((m) => ({ default: m.SettingsPage })),
);
const CompletionPage = lazy(() =>
  import('./routes/completion').then((m) => ({ default: m.CompletionPage })),
);

export const routeTree = rootRoute.addChildren([
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/',
    component: withSuspense(OverviewPage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/analyze',
    component: withSuspense(AnalyzePage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/compare',
    component: withSuspense(ComparePage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/batch',
    component: withSuspense(BatchPage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/subscriptions',
    component: withSuspense(SubscriptionsPage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/usage',
    component: withSuspense(UsagePage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/llm',
    component: withSuspense(LlmPage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/settings',
    component: withSuspense(SettingsPage),
  }),
  createRoute({
    getParentRoute: () => rootRoute,
    path: '/completion',
    component: withSuspense(CompletionPage),
  }),
]);