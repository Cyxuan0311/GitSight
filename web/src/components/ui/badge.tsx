import { cva, type VariantProps } from 'class-variance-authority';
import type { HTMLAttributes } from 'react';
import { useTranslation } from 'react-i18next';

import { cn } from '@/lib/cn';

const badgeVariants = cva(
  'inline-flex items-center gap-1 rounded-full border px-2 py-0.5 text-xs font-medium whitespace-nowrap',
  {
    variants: {
      tone: {
        neutral: 'border-line bg-raised text-ink-soft',
        accent: 'border-transparent bg-accent-soft text-accent',
        // State colours track the exporter's open/closed/merged palette so the
        // console and a downloaded report agree.
        open: 'border-transparent bg-open/12 text-open',
        closed: 'border-transparent bg-closed/12 text-closed',
        merged: 'border-transparent bg-merged/12 text-merged',
        warn: 'border-transparent bg-warn/15 text-warn',
        danger: 'border-transparent bg-danger/12 text-danger',
        ok: 'border-transparent bg-ok/12 text-ok',
      },
    },
    defaultVariants: { tone: 'neutral' },
  },
);

export interface BadgeProps
  extends HTMLAttributes<HTMLSpanElement>,
    VariantProps<typeof badgeVariants> {}

export function Badge({ className, tone, ...props }: BadgeProps) {
  return <span className={cn(badgeVariants({ tone }), className)} {...props} />;
}

/**
 * Pull request and issue states, mapped to the shared badge palette.
 *
 * The state is the platform's own word, and the console speaks the reader's
 * language, so the label is translated; a state the table does not know keeps
 * its original spelling rather than being shown as an empty badge.
 */
export function StateBadge({ state }: { state: string }) {
  const { t, i18n } = useTranslation();
  const tone =
    state === 'merged' ? 'merged' : state === 'open' ? 'open' : state === 'closed' ? 'closed' : 'neutral';
  const key = `state.${state}`;
  const label = i18n.exists(key) ? t(key) : state;
  return <Badge tone={tone}>{label}</Badge>;
}

export { badgeVariants };