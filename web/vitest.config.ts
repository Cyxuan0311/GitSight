import { defineConfig, mergeConfig } from 'vitest/config';

import viteConfig from './vite.config';

/**
 * Vitest reads its own config file; the `test` block is intentionally kept out
 * of vite.config.ts so `vite build` does not carry test-only types.
 */
export default mergeConfig(
  viteConfig,
  defineConfig({
    test: {
      environment: 'jsdom',
      globals: true,
      setupFiles: ['./src/test/setup.ts'],
      include: ['src/**/*.{test,spec}.{ts,tsx}'],
      // Every worker transforms modules through the single Vite server and
      // reports progress back to one main thread. Letting all sixteen cores do
      // that at once saturates it, and workers start losing RPC round trips —
      // the failure shows up as an unrelated-looking timeout in whichever file
      // happened to be running. A small pool keeps the main thread responsive.
      poolOptions: {
        threads: { maxThreads: 4, minThreads: 1 },
      },
      // Mounting the report and usage pages pulls recharts and the whole app graph
      // through Vite's transform pipeline, which on a cold cache — or on a slow
      // filesystem, where module loading costs hundreds of milliseconds per file —
      // can outrun these budgets. They bound a hung test, not a slow one.
      testTimeout: 60_000,
      hookTimeout: 300_000,
      teardownTimeout: 60_000,
    },
  }),
);
