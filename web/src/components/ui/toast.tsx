import { Check, CircleAlert, X } from 'lucide-react';
import { useTranslation } from 'react-i18next';

import { useToastStore } from '@/stores/toast';

import { cn } from '@/lib/cn';

/**
 * The stack of messages from the last few writes.
 *
 * It lives in the layout rather than in each page, so a message survives the
 * page that raised it and every page reports the same way.
 */
export function ToastViewport() {
  const { t } = useTranslation();
  const toasts = useToastStore((state) => state.toasts);
  const dismiss = useToastStore((state) => state.dismiss);

  if (toasts.length === 0) return null;

  return (
    <div
      className="pointer-events-none fixed right-4 bottom-4 z-50 flex w-[min(22rem,calc(100vw-2rem))] flex-col gap-2"
      role="status"
      aria-live="polite"
    >
      {toasts.map((entry) => {
        const Icon = entry.tone === 'success' ? Check : CircleAlert;
        return (
          <div
            key={entry.id}
            className={cn(
              'pointer-events-auto flex items-start gap-2 rounded-card border px-3 py-2 shadow-lg',
              'bg-surface transition-opacity',
              entry.tone === 'success' ? 'border-ok/40' : 'border-danger/50',
            )}
          >
            <Icon
              className={cn(
                'mt-0.5 size-4 shrink-0',
                entry.tone === 'success' ? 'text-ok' : 'text-danger',
              )}
            />
            <p className="min-w-0 flex-1 text-xs text-ink">{entry.message}</p>
            <button
              type="button"
              onClick={() => dismiss(entry.id)}
              aria-label={t('app.close')}
              className="shrink-0 rounded p-0.5 text-muted hover:bg-raised hover:text-ink"
            >
              <X className="size-3.5" />
            </button>
          </div>
        );
      })}
    </div>
  );
}
