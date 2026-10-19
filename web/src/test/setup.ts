import '@testing-library/jest-dom/vitest';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { cleanup, render, type RenderOptions, type RenderResult } from '@testing-library/react';
import { createElement } from 'react';
import { afterEach } from 'vitest';

import '../i18n';

afterEach(cleanup);

/**
 * Renders inside the same providers the app uses, so a component test does not
 * have to know that queries need a client. Each render gets a fresh client so
 * tests cannot leak cached state into one another.
 */
export function renderWithProviders(
  ui: React.ReactElement,
  options?: Omit<RenderOptions, 'wrapper'>,
): RenderResult {
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false, gcTime: 0, staleTime: 0 } },
  });
  return render(ui, {
    wrapper: ({ children }) =>
      createElement(QueryClientProvider, { client }, children),
    ...options,
  });
}

export * from '@testing-library/react';

// jsdom stubs neither of these reliably, and the router plus the charting
// components touch both on mount.
Object.defineProperty(window, 'matchMedia', {
  writable: true,
  configurable: true,
  value: (query: string) => ({
    matches: false,
    media: query,
    onchange: null,
    addListener: () => {},
    removeListener: () => {},
    addEventListener: () => {},
    removeEventListener: () => {},
    dispatchEvent: () => false,
  }),
});

window.scrollTo = () => {};

if (!('ResizeObserver' in globalThis)) {
  class ResizeObserverStub {
    observe() {}
    unobserve() {}
    disconnect() {}
  }
  Object.defineProperty(globalThis, 'ResizeObserver', {
    writable: true,
    value: ResizeObserverStub,
  });
}