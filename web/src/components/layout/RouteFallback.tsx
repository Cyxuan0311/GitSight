import { useRouterState } from '@tanstack/react-router';
import { useTranslation } from 'react-i18next';

import { Sidebar } from './Sidebar';
import { NAV_GROUPS } from './nav';

/**
 * What the console shows while a page chunk is still downloading.
 *
 * The router loads page modules lazily, so for a moment after a tab click the
 * next page's component does not exist yet. Rendering nothing there blanked the
 * whole app and made a slow chunk look like a crash; rendering the shell
 * instead keeps the header, the navigation and the page frame exactly where
 * they were. The rail is the real one, so switching tabs still works while the
 * previous request is in flight.
 *
 * The content area is a skeleton rather than a spinner because the pages differ
 * in height: a centred spinner would resize the moment the page lands, while a
 * stack of cards of roughly the right size keeps the scroll position stable.
 */
export function RouteFallback() {
  const { t } = useTranslation();
  const path = useRouterState({ select: (state) => state.location.pathname });

  return (
    <div className="flex min-h-full flex-col" aria-busy="true">
      {/* Only the fact of loading is announced; the skeleton itself is
       * decorative, and the real rail below stays navigable. */}
      <span role="status" className="sr-only">
        {t('app.loading')}
      </span>

      {/* A one-pixel sweep at the very top says "loading" without costing the
       * content area any space or covering the page below it. */}
      <div className="pointer-events-none fixed inset-x-0 top-0 z-50 h-0.5 overflow-hidden" aria-hidden>
        <span className="animate-route-load absolute inset-y-0 left-0 w-[28%] rounded-full bg-accent" />
      </div>

      <header className="sticky top-0 z-30 border-b border-line bg-canvas/85 backdrop-blur-md">
        <div className="flex h-14 items-center gap-3 px-4 md:px-6">
          <span className="animate-shimmer block size-7 shrink-0 rounded-md bg-raised" aria-hidden />
          <div className="min-w-0 flex-1 space-y-1.5">
            <span className="animate-shimmer block h-3 w-32 rounded bg-raised" aria-hidden />
            <span className="animate-shimmer block h-2.5 w-48 rounded bg-raised" aria-hidden />
          </div>
          <span className="animate-shimmer hidden h-5 w-20 rounded-field bg-raised sm:block" aria-hidden />
          <span className="animate-shimmer hidden h-6 w-16 rounded-field bg-raised sm:block" aria-hidden />
          <span className="animate-shimmer block size-7 rounded-full bg-raised" aria-hidden />
        </div>
      </header>

      <div className="flex flex-1">
        <Sidebar groups={NAV_GROUPS} currentPath={path} />

        <main className="min-w-0 flex-1">
          <div className="mx-auto w-full max-w-[1600px] space-y-5 p-4 md:p-6">
            <div className="flex items-center gap-3">
              <span className="animate-shimmer block size-9 shrink-0 rounded-field bg-raised" aria-hidden />
              <div className="space-y-2">
                <span className="animate-shimmer block h-4 w-44 rounded bg-raised" aria-hidden />
                <span className="animate-shimmer block h-2.5 w-64 rounded bg-raised" aria-hidden />
              </div>
            </div>

            <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-4">
              {Array.from({ length: 4 }, (_, index) => (
                <div key={index} className="rounded-card border border-line bg-surface p-4">
                  <span className="animate-shimmer block h-2.5 w-20 rounded bg-raised" aria-hidden />
                  <span className="animate-shimmer mt-3 block h-7 w-28 rounded bg-raised" aria-hidden />
                  <span className="animate-shimmer mt-4 block h-2.5 w-32 rounded bg-raised" aria-hidden />
                </div>
              ))}
            </div>

            <div className="grid gap-4 lg:grid-cols-[1.6fr_1fr]">
              <div className="rounded-card border border-line bg-surface p-4">
                <span className="animate-shimmer block h-3 w-36 rounded bg-raised" aria-hidden />
                <span className="animate-shimmer mt-4 block h-64 w-full rounded bg-raised" aria-hidden />
              </div>
              <div className="rounded-card border border-line bg-surface p-4">
                <span className="animate-shimmer block h-3 w-28 rounded bg-raised" aria-hidden />
                <div className="mt-4 space-y-3">
                  {Array.from({ length: 5 }, (_, index) => (
                    <span
                      key={index}
                      className="animate-shimmer block h-7 w-full rounded bg-raised"
                      aria-hidden
                    />
                  ))}
                </div>
              </div>
            </div>
          </div>
        </main>
      </div>

      {/* The skeleton is decorative, so the only thing announced is the fact
       * that the page is loading (see the status text above). */}
    </div>
  );
}
