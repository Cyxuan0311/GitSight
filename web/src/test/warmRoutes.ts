/**
 * Pages are behind React.lazy, so a suite that mounts them has to pull the
 * modules in itself rather than discover them through the router's boundary
 * mid-test.
 *
 * Ordering is the point. recharts arrives with the report and usage pages and
 * is by far the most expensive module in the app — roughly forty times the
 * cost of everything else combined. Issued alongside the other routes it queues
 * behind them and a single transform can outlast the worker's RPC timeout,
 * which surfaces as `[vitest-worker]: Timeout calling "fetch"` in whichever
 * suite happened to be scheduled during the cold start. Importing it on its own
 * first gives it an uncontended window; the routes that follow are then cheap.
 */

export async function warmRouteModules(...routes: string[]): Promise<void> {
  await import('recharts');
  await Promise.all(routes.map((route) => import(/* @vite-ignore */ route)));
}