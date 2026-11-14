/**
 * The shared box style for form controls.
 *
 * Kept in its own module so the date field can reuse it without importing the
 * whole field module, which exports components and would trip the fast-refresh
 * boundary.
 */
export const fieldBase =
  'w-full rounded-field border border-line-strong bg-canvas px-3 text-sm text-ink ' +
  'placeholder:text-muted transition-colors focus:border-accent focus:outline-none ' +
  'disabled:opacity-60 read-only:bg-raised';
