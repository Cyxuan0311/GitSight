import { QueryErrorResetBoundary, useQuery } from '@tanstack/react-query';
import { AlertTriangle, PlugZap } from 'lucide-react';
import { Component, type ReactNode } from 'react';
import { useTranslation } from 'react-i18next';

import { Button } from '@/components/ui/button';
import { EmptyState, ErrorState, Spinner } from '@/components/ui/feedback';
import { ApiError } from '@/lib/api';

/**
 * Renders the right recovery affordance for a failed query. A dead daemon is
 * the single most common failure here, so it gets its own message pointing at
 * the command that fixes it rather than a generic "something went wrong".
 */
export function QueryState({
  query,
  children,
  rows = 3,
}: {
  query: Pick<
    ReturnType<typeof useQuery>,
    'isPending' | 'isError' | 'error' | 'refetch' | 'data'
  >;
  children: ReactNode;
  rows?: number;
}) {
  const { t } = useTranslation();

  if (query.isPending) return <LoadingBlock rows={rows} />;

  if (query.isError) {
    const error = query.error;
    if (error instanceof ApiError && error.status === 0) {
      return (
        <EmptyState
          icon={<PlugZap />}
          title={t('errors.network')}
          hint={t('errors.serverHint')}
          action={
            <Button variant="outline" size="sm" onClick={() => void query.refetch()}>
              {t('app.retry')}
            </Button>
          }
        />
      );
    }
    return (
      <ErrorState
        title={error instanceof Error ? error.message : t('errors.loadFailed')}
        hint={error instanceof ApiError && error.status === 404 ? t('errors.notFound') : undefined}
        onRetry={() => void query.refetch()}
      />
    );
  }

  return <>{children}</>;
}

/**
 * Placeholder for content that has not arrived.
 *
 * `centered` is for a pane whose whole job is to wait -- a tab panel that swaps
 * its content on activation -- where a stack of left-aligned skeleton cards reads
 * as "the page is broken" and jitters the layout it sits in. Centring it keeps
 * the pane stable and says plainly that something is coming.
 */
export function LoadingBlock({
  rows = 3,
  centered = false,
  height = 320,
}: {
  rows?: number;
  centered?: boolean;
  /** Reserved height for the centred variant, so switching tabs does not jump. */
  height?: number;
}) {
  const { t } = useTranslation();

  if (centered) {
    return (
      <div
        className="grid place-items-center rounded-card border border-line bg-surface"
        style={{ minHeight: height }}
        aria-busy
        role="status"
      >
        <div className="flex flex-col items-center gap-3">
          <Spinner size="lg" className="text-accent" />
          <span className="text-xs text-muted">{t('app.loading')}</span>
        </div>
      </div>
    );
  }

  return (
    <div className="space-y-3" aria-busy>
      {Array.from({ length: rows }, (_, index) => (
        <div key={index} className="rounded-card border border-line bg-surface p-4">
          <div className="animate-shimmer h-3 w-32 rounded bg-raised" />
          <div className="animate-shimmer mt-3 h-8 w-full rounded bg-raised" />
        </div>
      ))}
    </div>
  );
}

interface BoundaryProps {
  children: ReactNode;
  fallback?: ReactNode;
}

/** Catches render-time throws, which a query error boundary cannot. */
class ErrorBoundary extends Component<BoundaryProps, { error: Error | null }> {
  override state = { error: null as Error | null };

  static getDerivedStateFromError(error: Error) {
    return { error };
  }

  override render() {
    if (this.state.error) {
      return (
        <div className="p-6">
          <ErrorState
            title={this.state.error.message}
            onRetry={() => this.setState({ error: null })}
          />
        </div>
      );
    }
    return this.props.children ?? this.props.fallback;
  }
}

/** Wraps a route so a crashed page resets cleanly instead of blanking the app. */
export function ApiErrorBoundary({ children }: BoundaryProps) {
  const { t } = useTranslation();
  return (
    <QueryErrorResetBoundary>
      {({ reset }) => (
        <ErrorBoundary
          fallback={
            <div className="p-6">
              <ErrorState title={t('errors.generic')} onRetry={reset} />
            </div>
          }
        >
          <button type="button" className="hidden" onClick={reset} aria-hidden />
          {children}
        </ErrorBoundary>
      )}
    </QueryErrorResetBoundary>
  );
}

/** Inline banner for a non-blocking failure inside an otherwise usable page. */
export function InlineError({ message }: { message: string }) {
  return (
    <div className="flex items-start gap-2 rounded-card border border-danger/40 bg-danger/8 p-3 text-sm text-danger">
      <AlertTriangle className="mt-0.5 size-4 shrink-0" />
      <span className="min-w-0 flex-1 break-words">{message}</span>
    </div>
  );
}