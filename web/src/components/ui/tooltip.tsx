import { Tooltip as BaseTooltip } from '@base-ui/react/tooltip';
import { useState, type ReactNode } from 'react';

import { cn } from '@/lib/cn';

/**
 * Tooltips need a provider per subtree, and most of this app's icons sit in a
 * long sidebar rail where the label is hidden. This wrapper supplies one
 * implicitly so call sites stay a single element.
 */
export function Tooltip({ content, children }: { content: ReactNode; children: ReactNode }) {
  const [open, setOpen] = useState(false);
  if (!content) return <>{children}</>;
  return (
    <BaseTooltip.Provider delay={300} closeDelay={0}>
      <BaseTooltip.Root open={open} onOpenChange={setOpen}>
        <BaseTooltip.Trigger
          render={children as React.ReactElement<Record<string, unknown>>}
          className={cn('inline-flex', (children as React.ReactElement<{ className?: string }>).props?.className)}
        />
        <BaseTooltip.Portal>
          <BaseTooltip.Positioner sideOffset={6}>
            <BaseTooltip.Popup
              className={cn(
                'z-50 max-w-xs rounded-md border border-line bg-surface px-2 py-1',
                'text-xs text-ink shadow-lg',
                'transition-opacity data-[ending-style]:opacity-0',
              )}
            >
              {content}
            </BaseTooltip.Popup>
          </BaseTooltip.Positioner>
        </BaseTooltip.Portal>
      </BaseTooltip.Root>
    </BaseTooltip.Provider>
  );
}