import { cva } from 'class-variance-authority';
import { forwardRef, type HTMLAttributes } from 'react';
import { useTranslation } from 'react-i18next';

import { cn } from '@/lib/cn';

export function Skeleton({ className, ...props }: HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn('animate-shimmer rounded-md bg-raised', className)}
      aria-hidden
      {...props}
    />
  );
}

export function EmptyState({
  title,
  hint,
  icon,
  action,
  className,
}: {
  title: string;
  hint?: string;
  icon?: React.ReactNode;
  action?: React.ReactNode;
  className?: string;
}) {
  return (
    <div className={cn('flex flex-col items-center gap-3 px-6 py-12 text-center', className)}>
      {icon ? <div className="text-muted [&_svg]:size-7">{icon}</div> : null}
      <div className="space-y-1">
        <p className="text-sm font-medium text-ink">{title}</p>
        {hint ? <p className="mx-auto max-w-sm text-xs text-muted">{hint}</p> : null}
      </div>
      {action}
    </div>
  );
}

export function ErrorState({
  title,
  hint,
  onRetry,
  className,
}: {
  title: string;
  hint?: string;
  onRetry?: () => void;
  className?: string;
}) {
  const { t } = useTranslation();
  return (
    <div className={cn('flex flex-col items-center gap-3 px-6 py-10 text-center', className)}>
      <p className="text-sm font-medium text-danger">{title}</p>
      {hint ? <p className="mx-auto max-w-md text-xs text-muted">{hint}</p> : null}
      {onRetry ? (
        <button
          type="button"
          onClick={onRetry}
          className="rounded-field border border-line-strong px-3 py-1.5 text-xs font-medium text-ink hover:bg-raised"
        >
          {t('app.retry')}
        </button>
      ) : null}
    </div>
  );
}

const spinnerVariants = cva(
  'inline-block shrink-0 rounded-full border-2 border-current border-t-transparent',
  {
    variants: {
      size: { sm: 'size-3', md: 'size-4', lg: 'size-6' },
    },
    defaultVariants: { size: 'md' },
  },
);

export function Spinner({
  size,
  className,
  label,
}: {
  size?: 'sm' | 'md' | 'lg';
  className?: string;
  label?: string;
}) {
  const { t } = useTranslation();
  return (
    <span role="status" aria-label={label ?? t('app.loading')}>
      <span className={cn('animate-spin', spinnerVariants({ size }), className)} />
    </span>
  );
}

export const Table = forwardRef<HTMLTableElement, HTMLAttributes<HTMLTableElement>>(
  ({ className, ...props }, ref) => (
    <div className="w-full overflow-x-auto scrollbar-thin">
      <table ref={ref} className={cn('w-full caption-bottom text-sm', className)} {...props} />
    </div>
  ),
);
Table.displayName = 'Table';

export function Th({ className, ...props }: HTMLAttributes<HTMLTableCellElement>) {
  return (
    <th
      className={cn(
        'border-b border-line px-3 py-2 text-left align-middle text-xs font-medium text-muted whitespace-nowrap',
        className,
      )}
      {...props}
    />
  );
}

export function Td({ className, ...props }: HTMLAttributes<HTMLTableCellElement>) {
  return (
    <td className={cn('border-b border-line/70 px-3 py-2 align-middle text-ink-soft', className)} {...props} />
  );
}

/** Thin progress meter; `value` is a 0..1 fraction or null when indeterminate. */
export function Progress({
  value,
  className,
  label,
}: {
  value: number | null;
  className?: string;
  label?: string;
}) {
  const pct = value === null ? null : Math.max(0, Math.min(1, value));
  return (
    <div
      role="progressbar"
      aria-valuenow={pct === null ? undefined : Math.round(pct * 100)}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-label={label}
      className={cn('h-1.5 w-full overflow-hidden rounded-full bg-raised', className)}
    >
      <div
        className={cn(
          'h-full rounded-full bg-accent transition-[width] duration-300',
          pct === null && 'w-1/3 animate-pulse',
        )}
        style={pct === null ? undefined : { width: `${pct * 100}%` }}
      />
    </div>
  );
}